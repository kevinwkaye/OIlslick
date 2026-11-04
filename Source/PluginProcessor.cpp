#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

// Several OilSlick windows can be open at once. Footswitches drive one of them at a time:
// the one whose panel was last touched, or the next one along when C+D are tapped.
namespace
{
SpinLock focusLock;
Array<OilSlickProcessor*> instances;
std::atomic<OilSlickProcessor*> focused { nullptr };

// set while a message from the footswitch device is being handled on the MIDI thread
thread_local bool fromDeviceNow = false;

// footswitch device messages go to the copy that is learning, else the copy in charge
void sendToFocused (const MidiMessage& m)
{
    const SpinLock::ScopedLockType l (focusLock);
    OilSlickProcessor* target = nullptr;
    for (auto* p : instances) if (p->isLearning()) target = p;
    if (target == nullptr) target = focused.load();
    if (target == nullptr && ! instances.isEmpty()) target = instances.getLast();
    if (target != nullptr) target->handleDirectMidi (m);
}
} // namespace

OilSlickProcessor::OilSlickProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "OilSlickState", createParameterLayout())
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            raw[rp->getParameterID()] = apvts.getRawParameterValue (rp->getParameterID());
    settings.load();
    settingsStamp = PedalSettings::file().getLastModificationTime();
    direct->setSink (&sendToFocused);
    direct->setWanted (settings.footDevice);
    footswitches.onEvent = [this] (int sw, FootswitchLogic::Event e) { onFootEvent (sw, e); };
    {
        const SpinLock::ScopedLockType l (focusLock);
        instances.add (this);
    }
    focused.store (this);
    startTimerHz (100);
}

OilSlickProcessor::~OilSlickProcessor()
{
    stopTimer();
    const SpinLock::ScopedLockType l (focusLock);
    instances.removeAllInstancesOf (this);
    if (focused.load() == this) focused.store (instances.isEmpty() ? nullptr : instances.getLast());
}

void OilSlickProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    keeper.prepare (sampleRate);
}

bool OilSlickProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != AudioChannelSet::stereo()) return false;
    return in == AudioChannelSet::stereo() || in == AudioChannelSet::mono();
}

os::EngineParams OilSlickProcessor::readParams()
{
    using namespace ParamIDs;
    os::EngineParams p;
    p.pitch = f (pitch);
    p.primary = f (primary);
    p.secondary = f (secondary);
    p.tracking = f (tracking);
    p.tone = f (tone);
    p.magic = f (magic);
    p.magicOn = flag (magicOn);
    p.active = ! flag (bypass);
    p.trails = flag (trails);
    p.freeze = flag (freeze);
    p.jump = flag (jump);
    p.dive = flag (dive);
    p.runaway = flag (runaway);
    p.version = idx (version);
    return p;
}

void OilSlickProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() < 2) return;
    if (getTotalNumInputChannels() == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);

    for (const auto meta : midi) handleMidi (meta.getMessage(), false);

    const auto p = readParams();
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    keeper.pushInput (L, R, n);
    engine.process (L, R, L, R, n, p);
    keeper.pushOutput (L, R, n);
}

// =====================================================================================
// MIDI: footswitches, expression and Keeper. The pedal itself has no MIDI, so there is no
// original CC chart to collide with the footswitch and expression defaults.
// =====================================================================================
void OilSlickProcessor::queue (int kind, int a)
{
    if (fromDeviceNow)
    {
        const auto scope = directFifo.write (1);
        if (scope.blockSize1 > 0) directPending[(size_t) scope.startIndex1] = { kind, a };
        return;
    }
    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0) pending[(size_t) scope.startIndex1] = { kind, a };
}

void OilSlickProcessor::handleDirectMidi (const MidiMessage& m)
{
    // several MIDI devices can call in on their own threads
    const SpinLock::ScopedLockType l (directWriteLock);
    fromDeviceNow = true;
    handleMidi (m, true);
    fromDeviceNow = false;
}

void OilSlickProcessor::setFootDevice (const String& device)
{
    settings.footDevice = device;
    settings.save();
    settingsStamp = PedalSettings::file().getLastModificationTime();
    direct->setWanted (device);
}

void OilSlickProcessor::handleMidi (const MidiMessage& m, bool fromDevice)
{
    if (! m.isController()) return;
    const int ch = m.getChannel(), cc = m.getControllerNumber(), val = m.getControllerValue();

    if (learnTarget.load() != LearnNone)
    {
        if (val > 0) queue (Learned, ch * 128 + cc);
        return;
    }

    // Straight from the footswitch device, any channel will do: it's the footswitch's own
    // cable, and some footswitches ignore their channel setting. Through Ableton, the
    // channel must match. While the device is read directly, the same messages arriving
    // through Ableton are ignored, so nothing happens twice.
    if (! fromDevice && pedalsDirect()) return;

    int foot = -1;
    for (int k = 0; k < 4 && foot < 0; ++k)
        if ((fromDevice || ch == settings.fsChannel[(size_t) k]) && cc == settings.fsCC[(size_t) k]) foot = k;
    const bool keep = foot < 0 && settings.keeperCC >= 0 && (fromDevice || ch == settings.keeperChannel) && cc == settings.keeperCC;
    const bool expression = foot < 0 && ! keep && (fromDevice || ch == settings.expChannel) && cc == settings.expCC;
    if (! (foot >= 0 || keep || expression)) return;   // anything else, sustain pedal included, is ignored

    if (! fromDevice)
    {
        // The copy Ableton sends footswitch MIDI to takes charge, unless the copy in
        // charge is fed too. A newly loaded copy with no MIDI track can't hold the pedals.
        hostFeedsPedals.store (true);
        const SpinLock::ScopedLockType l (focusLock);
        auto* fc = focused.load();
        if (fc != this && (fc == nullptr || ! fc->hostFeedsPedals.load())) focused.store (this);
        if (focused.load() != this) return;
    }
    if (foot >= 0) queue (val >= 64 ? FootPress : FootRelease, foot);
    else if (keep) { if (val >= 64) queue (KeepCC, 0); }
    else queue (Expression, val);
}

void OilSlickProcessor::setNorm (const String& id, float v01)
{
    if (auto* p = apvts.getParameter (id))
    {
        v01 = jlimit (0.0f, 1.0f, v01);
        if (std::abs (p->getValue() - v01) < 1.0e-5f) return;
        p->beginChangeGesture();
        p->setValueNotifyingHost (v01);
        p->endChangeGesture();
    }
}

void OilSlickProcessor::setPlain (const String& id, float plain)
{
    if (auto* p = apvts.getParameter (id)) setNorm (id, p->convertTo0to1 (plain));
}

void OilSlickProcessor::say (const String& s)
{
    uiMessage = s;
    uiMessageAt = Time::getMillisecondCounterHiRes() * 0.001;
}

void OilSlickProcessor::timerCallback() { pump (Time::getMillisecondCounterHiRes() * 0.001); }

void OilSlickProcessor::pump (double now)
{
    // settings changed by another copy, or another Flip Fella plugin: pick them up
    if (now >= nextSettingsCheck)
    {
        nextSettingsCheck = now + 1.0;
        const auto stamp = PedalSettings::file().getLastModificationTime();
        if (stamp != settingsStamp && learnTarget.load() == LearnNone && ! calibrating)
        {
            settingsStamp = stamp;
            settings.load();
            direct->setWanted (settings.footDevice);
        }
        const bool d = pedalsDirect();
        if (d != wasDirect)
        {
            wasDirect = d;
            if (d && focused.load() == this) say ("FOOTSWITCHES READY");
        }
    }

    auto apply = [&] (const std::array<Pending, 512>& from, int start, int size)
    {
        for (int i = start; i < start + size; ++i)
        {
            const auto& q = from[(size_t) i];
            switch (q.kind)
            {
                case FootPress: footswitches.press (q.a, now); break;
                case FootRelease: footswitches.release (q.a, now); break;
                case KeepCC: keepNow(); break;
                case Expression:
                {
                    expMovedAt = now;
                    if (calibrating)
                    {
                        if (calFirst < 0) calFirst = q.a;
                        calLo = jmin (calLo, q.a);
                        calHi = jmax (calHi, q.a);
                        uiExp.store ((float) q.a / 127.0f);
                        break;
                    }
                    expTargetValue = settings.expressionValue (q.a);
                    if (expSmoothed < 0.0f) expSmoothed = expTargetValue;
                    break;
                }
                case Learned:
                {
                    const int ch = q.a / 128, cc = q.a % 128;
                    const int what = learnTarget.exchange (LearnNone);
                    if (what >= 0 && what < 4)
                    {
                        settings.fsChannel[(size_t) what] = ch;
                        settings.fsCC[(size_t) what] = cc;
                        say ("FOOTSWITCH " + String::charToString ((juce_wchar) ('A' + what)) + " IS CC " + String (cc));
                    }
                    else if (what == LearnKeeper)
                    {
                        settings.keeperChannel = ch;
                        settings.keeperCC = cc;
                        say ("KEEPER IS CC " + String (cc));
                    }
                    else if (what == LearnExpression)
                    {
                        settings.expChannel = ch;
                        settings.expCC = cc;
                        say ("EXPRESSION IS CC " + String (cc));
                    }
                    settings.save();
                    settingsStamp = PedalSettings::file().getLastModificationTime();
                    break;
                }
                default: break;
            }
        }
    };
    {
        const auto scope = fifo.read (fifo.getNumReady());
        apply (pending, scope.startIndex1, scope.blockSize1);
        apply (pending, scope.startIndex2, scope.blockSize2);
    }
    {
        const auto scope = directFifo.read (directFifo.getNumReady());
        apply (directPending, scope.startIndex1, scope.blockSize1);
        apply (directPending, scope.startIndex2, scope.blockSize2);
    }

    // a remote controller pressing the footswitches through their parameters
    for (int k = 0; k < 4; ++k)
    {
        const bool down = flag (ParamIDs::foot[k]);
        if (down != remoteFoot[(size_t) k])
        {
            remoteFoot[(size_t) k] = down;
            if (down) footswitches.press (k, now);
            else footswitches.release (k, now);
        }
    }
    if (flag (ParamIDs::keeper)) { setPlain (ParamIDs::keeper, 0.0f); keepNow(); }
    if (flag (ParamIDs::tame)) { setPlain (ParamIDs::tame, 0.0f); tame(); }

    // expression, smoothed
    if (expTargetValue >= 0.0f)
    {
        const float alpha = jmap (f (ParamIDs::expSmooth), 1.0f, 0.04f);
        expSmoothed += (expTargetValue - expSmoothed) * alpha;
        const bool settled = std::abs (expTargetValue - expSmoothed) < 0.0005f;
        if (settled) expSmoothed = expTargetValue;
        applyExpression (expSmoothed);
        if (settled) expTargetValue = -1.0f;   // settled: let the knob be moved by hand
    }

    footswitches.tick (now);
    int mask = 0;
    for (int k = 0; k < 4; ++k) if (footswitches.isDown (k)) mask |= 1 << k;
    uiFootDown.store (mask);

    if (calibrating && now > calibrateUntil)
    {
        calibrating = false;
        if (calHi - calLo > 20)
        {
            // whichever end the pedal started at is the heel
            const bool startedLow = std::abs (calFirst - calLo) < std::abs (calFirst - calHi);
            settings.heel = startedLow ? calLo : calHi;
            settings.toe = startedLow ? calHi : calLo;
            settings.save();
            settingsStamp = PedalSettings::file().getLastModificationTime();
            say ("EXPRESSION CALIBRATED");
        }
        else
            say ("NO MOVEMENT, TRY AGAIN");
    }
}

void OilSlickProcessor::applyExpression (float v)
{
    uiExp.store (v);
    const float x = jmap (v, f (ParamIDs::expMin), f (ParamIDs::expMax));
    const auto& targets = names::expTargets();
    const int t = jlimit (0, (int) targets.size() - 1, idx (ParamIDs::expTarget));
    if (t == names::ExpPitch) setPlain (ParamIDs::pitch, expressionToPitch (x));
    else if (t == names::ExpMagic)
    {
        setPlain (ParamIDs::magic, expressionToMagic (x));
        if (! flag (ParamIDs::magicOn) && x > 0.02f) setPlain (ParamIDs::magicOn, 1.0f);   // a pedal on Magic should be heard
    }
    else setNorm (targets[(size_t) t].id, x);
}

// =====================================================================================
// Footswitches, based on the pedal's two. A is Activate, B is Magic (tap to latch, hold
// for the runaway), C jumps and dives, D freezes. A+B tames, C+D passes the footswitches
// on, and holding C+D for a second keeps.
// =====================================================================================
void OilSlickProcessor::onFootEvent (int sw, FootswitchLogic::Event e)
{
    using E = FootswitchLogic;
    if (e == E::Down || e == E::ComboAB || e == E::ComboCD) uiFootPulse.fetch_add (1);

    switch (e)
    {
        case E::ComboHoldCD:
            focused.store (this);   // Keeper on the plugin you were playing
            keepNow();
            return;
        case E::ComboCD:
        {
            if (dDownToggled) { toggle (ParamIDs::freeze); dDownToggled = false; }   // D was half of the pair
            const SpinLock::ScopedLockType l (focusLock);
            const int i = instances.indexOf (this);
            auto* next = instances.isEmpty() ? nullptr : instances[(i + 1) % instances.size()];
            focused.store (next);
            if (next != nullptr) next->say ("FOOTSWITCHES HERE");
            return;
        }
        case E::ComboAB:
            if (aDownToggled) { toggle (ParamIDs::bypass); aDownToggled = false; }   // A was half of the pair
            tame();
            return;
        case E::ComboHoldAB:
            return;
        default: break;
    }

    if (sw == 0)   // A: Activate. Acts on the press, so it's instant; held, it's momentary
    {
        if (e == E::Down) { toggle (ParamIDs::bypass); aDownToggled = true; say (flag (ParamIDs::bypass) ? "BYPASSED" : "ON"); }
        else if (e == E::Tap) aDownToggled = false;
        else if (e == E::HoldEnd) { toggle (ParamIDs::bypass); aDownToggled = false; }
    }
    else if (sw == 1)   // B: Magic. Tap latches it as on the pedal; hold runs away
    {
        if (e == E::Tap) { toggle (ParamIDs::magicOn); say (flag (ParamIDs::magicOn) ? "MAGIC ON" : "MAGIC OFF"); }
        else if (e == E::HoldStart) { setPlain (ParamIDs::runaway, 1.0f); say ("RUNAWAY"); }
        else if (e == E::HoldEnd) setPlain (ParamIDs::runaway, 0.0f);
    }
    else if (sw == 2)   // C: tap jumps a third up and back, hold dives
    {
        if (e == E::Tap) { toggle (ParamIDs::jump); say (flag (ParamIDs::jump) ? "JUMP UP A 3RD" : "BACK DOWN"); }
        else if (e == E::HoldStart) { setPlain (ParamIDs::dive, 1.0f); say ("DIVE"); }
        else if (e == E::HoldEnd) setPlain (ParamIDs::dive, 0.0f);
    }
    else if (sw == 3)   // D: Freeze on the press; held, only while held
    {
        if (e == E::Down)
        {
            toggle (ParamIDs::freeze);
            dDownToggled = true;
            dTurnedOn = flag (ParamIDs::freeze);
            say (dTurnedOn ? "FROZEN" : "THAWED");
        }
        else if (e == E::Tap) dDownToggled = false;
        else if (e == E::HoldEnd) { if (dTurnedOn) setPlain (ParamIDs::freeze, 0.0f); dDownToggled = dTurnedOn = false; }
    }
}

void OilSlickProcessor::footDown (int sw)
{
    focusHere();
    footswitches.press (sw, Time::getMillisecondCounterHiRes() * 0.001);
}

void OilSlickProcessor::footUp (int sw)
{
    footswitches.release (sw, Time::getMillisecondCounterHiRes() * 0.001);
}

void OilSlickProcessor::focusHere() { focused.store (this); }

void OilSlickProcessor::tame()
{
    engine.tame();
    setPlain (ParamIDs::runaway, 0.0f);
    setPlain (ParamIDs::freeze, 0.0f);
    uiTamePulse.fetch_add (1);
    say ("TAMED");
}

// =====================================================================================
// Footswitch learn and expression calibration
// =====================================================================================
void OilSlickProcessor::learnFootswitch (int sw)
{
    learnTarget.store (sw);
    say ("PRESS FOOTSWITCH " + String::charToString ((juce_wchar) ('A' + sw)));
}

void OilSlickProcessor::learnExpression()
{
    learnTarget.store (LearnExpression);
    say ("MOVE THE EXPRESSION PEDAL");
}

void OilSlickProcessor::calibrateExpression()
{
    calibrating = true;
    calibrateUntil = Time::getMillisecondCounterHiRes() * 0.001 + 6.0;
    calLo = 127; calHi = 0; calFirst = -1;
    say ("HEEL DOWN, THEN ROCK TO TOE");
}

void OilSlickProcessor::resetFootswitches()
{
    settings.resetFootswitches();
    settings.save();
    settingsStamp = PedalSettings::file().getLastModificationTime();
    say ("FOOTSWITCH DEFAULTS RESTORED");
}

void OilSlickProcessor::setCurve (int c)
{
    settings.curve = jlimit (0, 2, c);
    settings.save();
    settingsStamp = PedalSettings::file().getLastModificationTime();
}

// =====================================================================================
// Keeper
// =====================================================================================
void OilSlickProcessor::keepNow()
{
    DynamicObject::Ptr e = new DynamicObject();
    e->setProperty ("plugin", "OilSlick");
   #ifdef JucePlugin_VersionString
    e->setProperty ("version", JucePlugin_VersionString);
   #endif
    e->setProperty ("recreates", "EarthQuaker Devices Rainbow Machine");
    e->setProperty ("pedalVersion", idx (ParamIDs::version) == os::FirstEdition ? "First edition" : "V2");
    e->setProperty ("time", Time::getCurrentTime().toISO8601 (true));
    e->setProperty ("context", settings.keeperLive ? "Live" : "Studio");
    e->setProperty ("preset", "none");
    double bpm = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) bpm = *b;
    e->setProperty ("tempo", bpm);
    e->setProperty ("tempoFromHost", bpm > 0.0);

    // what you'd hear described: the moment, in words
    e->setProperty ("pitchNow", engine.uiPitch.load());
    e->setProperty ("magicNow", engine.uiMagic.load());
    e->setProperty ("loopGain", engine.uiLoopGain.load());
    e->setProperty ("runaway", engine.uiRunaway.load());
    e->setProperty ("frozen", engine.uiFrozen.load());

    DynamicObject::Ptr exp = new DynamicObject();
    const auto& targets = names::expTargets();
    exp->setProperty ("target", targets[(size_t) jlimit (0, (int) targets.size() - 1, idx (ParamIDs::expTarget))].label);
    exp->setProperty ("position", uiExp.load());
    exp->setProperty ("movedSecondsAgo", Time::getMillisecondCounterHiRes() * 0.001 - expMovedAt);
    e->setProperty ("expression", var (exp.get()));

    // every parameter, hidden ones included, as value and as shown on the panel
    DynamicObject::Ptr params = new DynamicObject();
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm))
        {
            if (rp->getParameterID() == ParamIDs::keeper) continue;
            DynamicObject::Ptr pv = new DynamicObject();
            pv->setProperty ("value", rp->convertFrom0to1 (rp->getValue()));
            pv->setProperty ("shown", rp->getCurrentValueAsText());
            params->setProperty (rp->getParameterID(), var (pv.get()));
        }
    e->setProperty ("parameters", var (params.get()));

    keeper.keep (e);
    uiKeepPulse.fetch_add (1);
    say ("KEPT");
}

File OilSlickProcessor::exportKeepers()
{
    const auto file = keeper.exportZip();
    if (file.existsAsFile()) { say ("EXPORTED TO DOCUMENTS"); file.revealToUser(); }
    else say ("NOTHING KEPT YET");
    return file;
}

void OilSlickProcessor::openKeepersFolder()
{
    Keeper::folder().createDirectory();
    Keeper::folder().startAsProcess();
}

void OilSlickProcessor::learnKeeper()
{
    learnTarget.store (LearnKeeper);
    say ("PRESS YOUR KEEPER SWITCH");
}

void OilSlickProcessor::setKeeperLive (bool live)
{
    settings.keeperLive = live;
    settings.save();
    settingsStamp = PedalSettings::file().getLastModificationTime();
}

// =====================================================================================
// State
// =====================================================================================
void OilSlickProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void OilSlickProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (ValueTree::fromXml (*xml));
    // nothing held down or frozen survives a reload
    for (auto* id : { ParamIDs::foot[0], ParamIDs::foot[1], ParamIDs::foot[2], ParamIDs::foot[3],
                      ParamIDs::keeper, ParamIDs::tame, ParamIDs::dive, ParamIDs::runaway, ParamIDs::freeze })
        setPlain (id, 0.0f);
}

AudioProcessorEditor* OilSlickProcessor::createEditor() { return new OilSlickEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new OilSlickProcessor(); }
