// Footswitch and expression test: the real OilSlick processor, driven only by plain MIDI
// press (127) and release (0) messages on the shipped defaults, as a footswitch sends them.
// Checks tap, hold, A+B and C+D combos, the 1 second C+D Keeper hold, the musical
// expression curve, the remote-control parameters, and that nothing else on the MIDI
// cable (sustain pedal, mod wheel, the wrong channel) changes anything.

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>

using namespace juce;

namespace
{
int failures = 0;

void expect (bool ok, const char* what)
{
    std::printf ("  %-62s %s\n", what, ok ? "ok" : "FAILED");
    if (! ok) ++failures;
}

struct Rig
{
    OilSlickProcessor proc;
    AudioBuffer<float> buffer { 2, 64 };
    double now = 100.0;

    Rig()
    {
        // the shipped defaults, whatever is saved on this computer
        auto& s = proc.pedals();
        s.resetFootswitches();
        s.expChannel = 16; s.expCC = 11;
        s.keeperChannel = 0; s.keeperCC = -1;
        s.heel = 0; s.toe = 127; s.curve = 0;
        proc.prepareToPlay (48000.0, 64);
        proc.apvts.getParameter (ParamIDs::expSmooth)->setValueNotifyingHost (0.0f);
        settle (0.05);
    }

    void cc (int number, int value, int channel = 16)
    {
        MidiBuffer midi;
        midi.addEvent (MidiMessage::controllerEvent (channel, number, value), 0);
        buffer.clear();
        proc.processBlock (buffer, midi);
        proc.pump (now);
    }

    // let time pass, pumping the message-thread work every 10 ms as the timer does
    void settle (double seconds)
    {
        for (double t = 0.0; t < seconds; t += 0.01)
        {
            now += 0.01;
            proc.pump (now);
        }
    }

    void press (int sw) { cc (20 + sw, 127); }
    void release (int sw) { cc (20 + sw, 0); }
    void tap (int sw) { press (sw); settle (0.1); release (sw); settle (0.05); }
    void hold (int sw, double seconds) { press (sw); settle (seconds); }

    bool on (const char* id) { return proc.apvts.getRawParameterValue (id)->load() > 0.5f; }
    float value (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); }
    void set (const char* id, float plain)
    {
        auto* p = proc.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
    }

    // every parameter except those a test expects to change
    std::map<String, float> snapshot()
    {
        std::map<String, float> m;
        for (auto* p : proc.getParameters())
            if (auto* rp = dynamic_cast<RangedAudioParameter*> (p)) m[rp->getParameterID()] = rp->getValue();
        return m;
    }
};
} // namespace

// --snapshot <folder>: render the panel in a few states as PNGs (needs a display, e.g. xvfb-run)
static int snapshots (const File& dir)
{
    dir.createDirectory();
    Rig r;
    std::unique_ptr<AudioProcessorEditor> ed (r.proc.createEditor());
    auto* editor = dynamic_cast<OilSlickEditor*> (ed.get());
    ed->setVisible (true);
    double phase = 0.0;
    auto play = [&] (double seconds)
    {
        for (double t = 0.0; t < seconds; t += 64.0 / 48000.0)
        {
            for (int i = 0; i < 64; ++i)
            {
                const float v = (float) (0.3 * std::sin (phase) * std::exp (-std::fmod (phase / 1382.0, 1.0) * 3.0));
                phase += 2.0 * MathConstants<double>::pi * 220.0 / 48000.0;
                r.buffer.setSample (0, i, v);
                r.buffer.setSample (1, i, v);
            }
            MidiBuffer none;
            r.proc.processBlock (r.buffer, none);
        }
        r.settle (0.01);
        MessageManager::getInstance()->runDispatchLoopUntil (400);
    };
    auto shot = [&] (const char* name)
    {
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        File f = dir.getChildFile (String (name) + ".png");
        f.deleteFile();
        FileOutputStream out (f);
        PNGImageFormat().writeImageToStream (img, out);
        std::printf ("  wrote %s\n", f.getFullPathName().toRawUTF8());
    };
    play (1.0); shot ("1_calm");
    r.set (ParamIDs::pitch, 3.0f); r.set (ParamIDs::magicOn, 1.0f); r.set (ParamIDs::magic, 0.7f); r.set (ParamIDs::secondary, 0.5f);
    play (1.2); shot ("2_pixie_trails");
    r.set (ParamIDs::runaway, 1.0f); play (2.5); shot ("3_runaway");
    r.set (ParamIDs::runaway, 0.0f); r.set (ParamIDs::freeze, 1.0f); play (1.0); shot ("4_freeze");
    r.set (ParamIDs::freeze, 0.0f); r.set (ParamIDs::bypass, 1.0f); r.set (ParamIDs::pitch, -4.0f); play (1.0); shot ("5_bypassed");
    r.set (ParamIDs::bypass, 0.0f); editor->setPage (1); play (0.5); shot ("6_pedal_page");
    return 0;
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI juce;
    if (argc > 2 && String (argv[1]) == "--snapshot") return snapshots (File (argv[2]));

    std::printf ("Footswitch A: Activate\n");
    {
        Rig r;
        r.tap (0);
        expect (r.on (ParamIDs::bypass), "tap A bypasses");
        r.tap (0);
        expect (! r.on (ParamIDs::bypass), "tap A again switches back on");
        r.hold (0, 1.0);
        expect (r.on (ParamIDs::bypass), "hold A: bypassed while held");
        r.release (0); r.settle (0.05);
        expect (! r.on (ParamIDs::bypass), "release A: back on (momentary)");
    }

    std::printf ("Footswitch B: Magic and the runaway\n");
    {
        Rig r;
        r.tap (1);
        expect (r.on (ParamIDs::magicOn), "tap B latches Magic on");
        r.tap (1);
        expect (! r.on (ParamIDs::magicOn), "tap B again latches it off");
        r.hold (1, 0.3);
        expect (! r.on (ParamIDs::runaway), "no runaway before the hold time");
        r.settle (0.5);
        expect (r.on (ParamIDs::runaway), "hold B runs away");
        expect (! r.on (ParamIDs::magicOn), "the runaway leaves the Magic switch alone");
        r.release (1); r.settle (0.05);
        expect (! r.on (ParamIDs::runaway), "release B reels it in");
    }

    std::printf ("Footswitch C: Jump and Dive\n");
    {
        Rig r;
        r.tap (2);
        expect (r.on (ParamIDs::jump), "tap C jumps");
        r.tap (2);
        expect (! r.on (ParamIDs::jump), "tap C again comes back");
        r.hold (2, 0.8);
        expect (r.on (ParamIDs::dive), "hold C dives");
        r.release (2); r.settle (0.05);
        expect (! r.on (ParamIDs::dive), "release C springs back");
        expect (! r.on (ParamIDs::jump), "a dive doesn't leave a jump behind");
    }

    std::printf ("Footswitch D: Freeze\n");
    {
        Rig r;
        r.tap (3);
        expect (r.on (ParamIDs::freeze), "tap D freezes");
        r.tap (3);
        expect (! r.on (ParamIDs::freeze), "tap D again thaws");
        r.hold (3, 0.8);
        expect (r.on (ParamIDs::freeze), "hold D: frozen while held");
        r.release (3); r.settle (0.05);
        expect (! r.on (ParamIDs::freeze), "release D thaws (momentary)");
    }

    std::printf ("Pairs\n");
    {
        Rig r;
        const int tamesBefore = r.proc.uiTamePulse.load();
        r.press (0); r.settle (0.03); r.press (1); r.settle (0.1); r.release (0); r.release (1); r.settle (0.1);
        expect (r.proc.uiTamePulse.load() == tamesBefore + 1, "A+B tames");
        expect (! r.on (ParamIDs::bypass), "A+B leaves bypass as it was");
        expect (! r.on (ParamIDs::magicOn), "A+B leaves Magic as it was");

        const int keepsBefore = r.proc.uiKeepPulse.load();
        r.press (3); r.settle (0.03); r.press (2); r.settle (0.1); r.release (2); r.release (3); r.settle (0.1);
        expect (! r.on (ParamIDs::freeze), "C+D tap (D first) leaves freeze as it was");
        expect (! r.on (ParamIDs::jump), "C+D tap leaves jump as it was");
        expect (r.proc.uiKeepPulse.load() == keepsBefore, "C+D tap doesn't keep");

        r.press (2); r.settle (0.03); r.press (3); r.settle (1.2);
        expect (r.proc.uiKeepPulse.load() == keepsBefore + 1, "C+D held 1 second keeps");
        r.release (2); r.release (3); r.settle (0.1);
        expect (! r.on (ParamIDs::dive) && ! r.on (ParamIDs::freeze), "C+D hold leaves dive and freeze alone");
        expect (r.proc.uiKeepPulse.load() == keepsBefore + 1, "only one Keeper per hold");
    }

    std::printf ("Expression on Pitch (musical heel to toe)\n");
    {
        Rig r;
        r.cc (11, 0); r.settle (0.05);
        expect (std::abs (r.value (ParamIDs::pitch) - os::kPitchMin) < 0.05f, "heel is a fourth down");
        r.cc (11, 127); r.settle (0.05);
        expect (std::abs (r.value (ParamIDs::pitch) - os::kPitchMax) < 0.05f, "toe is a third up");
        r.cc (11, 62); r.settle (0.05);
        const float a = r.value (ParamIDs::pitch);
        r.cc (11, 66); r.settle (0.05);
        const float b = r.value (ParamIDs::pitch);
        expect (std::abs (a) < 0.02f && std::abs (b) < 0.02f, "the middle rests at unison");
        // a straight line would sit 0.25 semitones from the nearest interval on average
        double off = 0.0;
        int count = 0;
        for (int v = 0; v < 1000; ++v)
        {
            const float x = (float) v / 999.0f;
            if (x > 0.46f && x < 0.54f) continue;
            const float st = expressionToPitch (x);
            off += std::abs (st - std::round (st));
            ++count;
        }
        expect (off / count < 0.22, "the sweep is drawn towards semitones");
    }

    std::printf ("Expression on Magic\n");
    {
        Rig r;
        r.set (ParamIDs::expTarget, (float) names::ExpMagic);
        r.cc (11, 127); r.settle (0.05);
        expect (r.value (ParamIDs::magic) > 0.98f, "toe is full Magic");
        expect (r.on (ParamIDs::magicOn), "moving the pedal switches Magic on");
        r.cc (11, 64); r.settle (0.05);
        expect (r.value (ParamIDs::magic) > 0.5f && r.value (ParamIDs::magic) < 0.75f, "half travel is pixie trails");
        expect (std::abs (r.value (ParamIDs::pitch)) < 0.001f, "Pitch is left alone");
    }

    std::printf ("Remote control through the parameter list\n");
    {
        Rig r;
        r.set (ParamIDs::foot[0], 1.0f); r.settle (0.05); r.set (ParamIDs::foot[0], 0.0f); r.settle (0.05);
        expect (r.on (ParamIDs::bypass), "footA parameter taps A");
        r.set (ParamIDs::foot[1], 1.0f); r.settle (0.8);
        expect (r.on (ParamIDs::runaway), "footB parameter held runs away");
        r.set (ParamIDs::foot[1], 0.0f); r.settle (0.05);
        const int keeps = r.proc.uiKeepPulse.load();
        r.set (ParamIDs::keeper, 1.0f); r.settle (0.05);
        expect (r.proc.uiKeepPulse.load() == keeps + 1 && ! r.on (ParamIDs::keeper), "keeper parameter keeps once and resets");
        const int tames = r.proc.uiTamePulse.load();
        r.set (ParamIDs::tame, 1.0f); r.settle (0.05);
        expect (r.proc.uiTamePulse.load() == tames + 1 && ! r.on (ParamIDs::tame), "tame parameter tames once and resets");
    }

    std::printf ("No clashes\n");
    {
        Rig r;
        const auto before = r.snapshot();
        // sustain pedal and mod wheel on the footswitch channel
        r.cc (64, 127); r.settle (0.6); r.cc (64, 0);
        r.cc (1, 100);
        // the footswitch and expression numbers on the wrong channel
        for (int ch : { 1, 2, 15 })
        {
            for (int n : { 20, 21, 22, 23 }) { r.cc (n, 127, ch); r.settle (0.6); r.cc (n, 0, ch); }
            r.cc (11, 0, ch);
        }
        r.settle (0.2);
        expect (r.snapshot() == before, "sustain, mod wheel and other channels change nothing");
    }

    std::printf ("\n%s: %d failed\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
