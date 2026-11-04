#pragma once

#include "Params.h"
#include "PedalControl.h"
#include "Keeper.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

class OilSlickProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    OilSlickProcessor();
    ~OilSlickProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "OilSlick"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter (ParamIDs::bypass); }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    os::OilEngine& getEngine() { return engine; }

    // ---- the four footswitches, from the panel's buttons (message thread) ----
    void footDown (int sw);
    void footUp (int sw);
    void tame();
    void focusHere();   // the panel was clicked: footswitches drive this copy

    // footswitch and expression setup
    PedalSettings& pedals() { return settings; }
    void learnFootswitch (int sw);
    void learnExpression();
    void calibrateExpression();
    void resetFootswitches();
    void setCurve (int c);

    // Keeper: log this moment, the last 8 seconds of audio included
    void keepNow();
    void addKeeperNote (int number, const juce::String& note) { keeper.addNote (number, note); }
    juce::File exportKeepers();
    void openKeepersFolder();
    void learnKeeper();
    bool keeperLive() const { return settings.keeperLive; }
    void setKeeperLive (bool live);
    int lastKeeper() const { return keeper.lastNumber(); }

    // footswitch MIDI read straight from the device, so every copy works without a MIDI track
    void handleDirectMidi (const juce::MidiMessage&);   // MIDI thread
    bool pedalsDirect() const { return direct->connected(); }
    juce::String footInputStatus() const { return direct->status(); }
    juce::String footDevice() const { return settings.footDevice; }
    void setFootDevice (const juce::String& device);
    std::atomic<bool> hostFeedsPedals { false };

    bool isLearning() const { return learnTarget != LearnNone; }
    int learningWhat() const { return learnTarget; }
    bool isCalibrating() const { return calibrating; }
    enum { LearnNone = -1, LearnExpression = 4, LearnKeeper = 5 };

    // ---- what the panel and Pebble show ----
    float expressionLevel() const { return uiExp.load(); }
    double lastExpressionMove() const { return expMovedAt; }
    std::atomic<int> uiFootPulse { 0 };       // counts footswitch presses: Pebble taps its foot
    std::atomic<int> uiKeepPulse { 0 };       // counts Keepers: Pebble nods
    std::atomic<int> uiTamePulse { 0 };       // Tame pressed: Pebble calms down
    std::atomic<int> uiFootDown { 0 };        // bitmask of footswitches held now
    juce::String uiMessage;                   // a short line for Pebble's status
    double uiMessageAt = 0.0;

    // Runs everything the timer does. Public so the footswitch test can drive it without a message loop.
    void pump (double now);

private:
    void timerCallback() override;
    os::EngineParams readParams();
    void handleMidi (const juce::MidiMessage&, bool fromDevice);
    void onFootEvent (int sw, FootswitchLogic::Event);
    void setNorm (const juce::String& id, float v01);
    void setPlain (const juce::String& id, float plain);
    bool flag (const char* id) const { return raw.at (id)->load() > 0.5f; }
    void toggle (const char* id) { setPlain (id, flag (id) ? 0.0f : 1.0f); }
    void say (const juce::String& s);
    void applyExpression (float v);

    os::OilEngine engine;
    Keeper keeper;

    std::map<juce::String, std::atomic<float>*> raw;
    float f (const char* id) const { return raw.at (id)->load (std::memory_order_relaxed); }
    int idx (const char* id) const { return (int) std::lround (f (id)); }

    // MIDI handed to the message thread
    struct Pending { int kind = 0; int a = 0; };
    enum PendingKind { FootPress, FootRelease, Expression, Learned, KeepCC };
    juce::AbstractFifo fifo { 512 };
    std::array<Pending, 512> pending;
    juce::AbstractFifo directFifo { 512 };    // the same, written from the MIDI thread
    std::array<Pending, 512> directPending;
    juce::SpinLock directWriteLock;
    juce::SharedResourcePointer<DirectMidiInput> direct;
    juce::Time settingsStamp;
    bool wasDirect = false;
    double nextSettingsCheck = 0.0;
    void queue (int kind, int a);

    PedalSettings settings;
    FootswitchLogic footswitches;
    std::atomic<int> learnTarget { LearnNone };
    bool calibrating = false;
    double calibrateUntil = 0.0;
    int calLo = 127, calHi = 0, calFirst = -1;
    double expMovedAt = 0.0;
    float expTargetValue = -1.0f, expSmoothed = -1.0f;
    std::array<bool, 4> remoteFoot {};        // last seen state of the footA..footD parameters

    // what a footswitch's press did, so a combo can undo it
    bool aDownToggled = false, dDownToggled = false, dTurnedOn = false;

    std::atomic<float> uiExp { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OilSlickProcessor)
};
