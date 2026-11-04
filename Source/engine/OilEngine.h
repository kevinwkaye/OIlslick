#pragma once

#include "PitchVoice.h"
#include <atomic>

namespace os
{
enum Version { V2 = 0, FirstEdition = 1 };

constexpr float kPitchMin = -5.0f;   // a fourth down
constexpr float kPitchMax = 4.0f;    // a major third up
constexpr float kJumpTo = 4.0f;      // footswitch C's Jump: a third up
constexpr float kMagicMax = 1.15f;   // where the runaway takes Magic: past the end of the knob

// Everything the engine needs, read once per block.
struct EngineParams
{
    float pitch = 0.0f;       // semitones, -5..+4, noon is unison
    float primary = 0.72f;    // 0..1, unity at 0.72 (about 2 o'clock)
    float secondary = 0.0f;   // -1..1: below zero an octave under the Primary, above an octave over, 0 off
    float tracking = 0.55f;   // 0..1: 0 is the longest lag, 1 is tight
    float tone = 0.6f;        // 0..1, dark to bright
    float magic = 0.4f;       // 0..1
    bool magicOn = false;     // the Magic switch
    bool active = true;       // false when bypassed
    bool trails = true;       // let the repeats ring out when bypassed
    bool freeze = false;
    bool jump = false;        // footswitch C tapped: Pitch jumps a third up
    bool dive = false;        // footswitch C held: Pitch glides to the bottom
    bool runaway = false;     // footswitch B held: Magic climbs into self-oscillation
    int version = V2;
};

// Shared tuning for each version. Nobody publishes measurements, so the first edition is
// tuned by ear from what reviews describe: quieter, noisier, a shorter Tracking range and
// a Magic knob that takes off sooner.
struct VersionVoice
{
    float maxTrackMs, wetGain, chorusCents, noise, magicUnity, aliasBoost;
    bool crush;
};
inline VersionVoice versionVoice (int v)
{
    if (v == FirstEdition) return { 500.0f, 0.5f, 7.0f, 2.0e-4f, 0.75f, 1.3f, true };
    return { 750.0f, 1.0f, 12.0f, 1.5e-5f, 0.85f, 1.0f, false };
}

// Tracking knob to lag in milliseconds: 2 ms at full clockwise, exponential to the maximum.
inline float trackingMs (float tracking, int version)
{
    const float maxMs = versionVoice (version).maxTrackMs;
    return 2.0f * std::pow (maxMs / 2.0f, 1.0f - clampf (tracking, 0.0f, 1.0f));
}

// Magic (with the runaway, up to 1.15) to loop gain. Below the unity point the repeats die
// away; past it the loop grows on its own and the soft clipper in the loop holds it, so it
// oscillates, then distorts as the gain keeps rising.
inline float magicToLoopGain (float m, int version)
{
    const float unity = versionVoice (version).magicUnity;
    if (m <= 0.0f) return 0.0f;
    if (m <= unity) return 0.985f * std::pow (m / unity, 1.3f);
    return 0.985f + (m - unity) / (kMagicMax - unity) * 0.75f;
}

class OilEngine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        delay.prepare ((int) (0.8 * sr) + 64);
        voice1.prepare (sr, 41.0f, 0x1234567u);
        voice2.prepare (sr, 29.0f, 0x7654321u);
        freezer.prepare (sr);
        chorus1.prepare (sr, 0.45f, 0xabcdefu);
        chorus2.prepare (sr, 0.31f, 0x13579bu);

        pitchSm.setTime (0.03f, sr);
        trackSm.setTime (0.25f, sr);     // slow, like a tape delay: moving Tracking bends the pitch
        primarySm.setTime (0.02f, sr);
        secondarySm.setTime (0.02f, sr);
        toneSm.setTime (0.03f, sr);
        magicSm.setTime (0.05f, sr);
        inGate.setTime (0.008f, sr);
        wetGate.setTime (0.02f, sr);
        diveIn = std::exp (-1.0f / (float) (0.45 * sr));
        diveOut = std::exp (-1.0f / (float) (0.18 * sr));
        runIn = std::exp (-1.0f / (float) (0.65 * sr));   // about 2 seconds to full runaway
        runOut = std::exp (-1.0f / (float) (0.25 * sr));
        reset();
    }

    void reset()
    {
        delay.clear();
        voice1.reset();
        voice2.reset();
        for (auto* b : { &toneL, &toneR }) b->reset();
        loopLp.reset();
        loopDc.reset();
        fb = 0.0f; held = 0.0f; holdCount = 0.0f;
        diveLevel = 0.0f; runLevel = 0.0f;
        primed = false;
    }

    // From any thread: kill the feedback and the trails right now.
    void tame() { tameRequest.store (true); }

    // In place is fine. Mono core, stereo in and out: the dry passes in stereo, the two
    // voices sit slightly apart.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, const EngineParams& p)
    {
        const VersionVoice vv = versionVoice (p.version);

        if (! primed)
        {
            pitchSm.reset (p.pitch);
            trackSm.reset (trackingMs (p.tracking, p.version) * 0.001f * (float) sr);
            primarySm.reset (p.primary);
            secondarySm.reset (p.secondary);
            toneSm.reset (p.tone);
            magicSm.reset (p.magicOn ? p.magic : 0.0f);
            inGate.reset (p.active ? 1.0f : 0.0f);
            wetGate.reset (p.active ? 1.0f : 0.0f);
            primed = true;
        }

        if (tameRequest.exchange (false)) tameCount = (int) (0.03 * sr);

        if (p.freeze && ! freezer.isFrozen()) freezer.engage();
        else if (! p.freeze && freezer.isFrozen()) freezer.release();

        const float trackTarget = trackingMs (p.tracking, p.version) * 0.001f * (float) sr;
        const float basePitch = clampf (p.jump ? kJumpTo : p.pitch, kPitchMin, kPitchMax);
        const float magicTarget = p.magicOn ? p.magic : 0.0f;
        float peakIn = 0.0f, peakOut = 0.0f, peakWet = 0.0f, effMagic = 0.0f, gain = 0.0f, shownPitch = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float xl = inL[i], xr = inR[i];
            const float x = 0.5f * (xl + xr);
            peakIn = std::max (peakIn, std::abs (x));

            // ---- control ----
            diveLevel = p.dive ? 1.0f + diveIn * (diveLevel - 1.0f) : diveOut * diveLevel;
            runLevel = p.runaway ? 1.0f + runIn * (runLevel - 1.0f) : runOut * runLevel;
            const float pitch = pitchSm.next (basePitch + (kPitchMin - basePitch) * diveLevel);
            const float m = magicSm.next (magicTarget);
            effMagic = m + (kMagicMax - m) * runLevel;
            gain = magicToLoopGain (effMagic, p.version);
            if (! p.active) gain = std::min (gain, 0.6f);   // bypassed trails always die away

            float tameFade = 1.0f;
            if (tameCount > 0)
            {
                tameFade = (float) tameCount / (float) (0.03 * sr);
                gain = 0.0f;
                if (--tameCount == 0) { delay.clear(); voice1.reset(); voice2.reset(); fb = 0.0f; runLevel = 0.0f; }
            }

            const float ig = inGate.next (p.active ? 1.0f : 0.0f);
            const float wg = wetGate.next (p.active || p.trails ? 1.0f : 0.0f) * tameFade;
            const float prim = primarySm.next (p.primary);
            const float sec = secondarySm.next (p.secondary);
            const float tone = toneSm.next (p.tone);
            const float lag = trackSm.next (trackTarget);

            if ((i & 31) == 0)
            {
                const float hz = 1200.0f * std::pow (16000.0f / 1200.0f, tone);
                toneL.setLowPass (hz, 0.6f, sr);
                toneR.setLowPass (hz, 0.6f, sr);
                loopLp.setCutoff (std::min (hz * 1.3f, 0.45f * (float) sr), sr);
            }

            // ---- the Tracking lag, which the Magic loop runs back through ----
            const float seed = vv.noise * noiseRng.next();
            delay.write (x * ig + fb + (gain > 0.0f ? seed : 0.0f));
            const float d = delay.read (lag);

            // ---- the two voices, each with its own chorus wobble ----
            const float c1 = vv.chorusCents * chorus1.next();
            const float c2 = vv.chorusCents * chorus2.next();
            const float r1 = semitonesToRatio (pitch + c1 * 0.01f);
            const float r2 = semitonesToRatio (pitch + (sec >= 0.0f ? 12.0f : -12.0f) + c2 * 0.01f);
            const float v1 = voice1.process (d, r1, 0.08f);
            const float v2 = voice2.process (d, r2, 0.11f);

            // ---- Magic: both voices feed back into the lag, so each pass is shifted again ----
            // It only works while the Primary or Secondary is up, as on the pedal.
            const float wP = std::min (1.0f, prim / 0.72f), wS = std::abs (sec);
            float loop = (wP * v1 + wS * v2) / std::max (1.0f, wP + wS) * gain;
            loop = loopDc.process (loopLp.process (loop));
            // pixie trails: an unfiltered sample-and-hold folds high notes back down
            const float alias = clampf ((effMagic - 0.35f) / 0.5f, 0.0f, 1.0f);
            if (alias > 0.0f)
            {
                const float hold = 1.0f + alias * 5.0f * vv.aliasBoost;
                holdCount += 1.0f;
                if (holdCount >= hold) { held = loop; holdCount -= hold; }
                loop += (held - loop) * alias * 0.85f;
            }
            if (vv.crush) loop = std::round (loop * 2048.0f) / 2048.0f;
            fb = softClip (loop * 1.15f) / 1.15f;

            // ---- out ----
            const float gP = (prim / 0.72f) * (prim / 0.72f);
            const float gS = std::abs (sec) * 1.2f;
            float wl = toneL.process (gP * v1 + 0.8f * gS * v2);
            float wr = toneR.process (0.8f * gP * v1 + gS * v2);
            freezer.record (0.5f * (wl + wr));
            const float fr = freezer.play();
            wl = (wl * wg + fr) * vv.wetGain;
            wr = (wr * wg + fr) * vv.wetGain;
            peakWet = std::max (peakWet, std::abs (wl));

            const float hiss = p.active ? vv.noise * 0.3f * noiseRng.next() : 0.0f;
            outL[i] = safetyLimit (xl + wl + hiss);
            outR[i] = safetyLimit (xr + wr + hiss);
            peakOut = std::max (peakOut, std::max (std::abs (outL[i]), std::abs (outR[i])));
            shownPitch = pitch;
        }

        uiInput.store (std::max (peakIn, uiInput.load() * 0.85f));
        uiOutput.store (std::max (peakOut, uiOutput.load() * 0.85f));
        uiWet.store (std::max (peakWet, uiWet.load() * 0.85f));
        uiPitch.store (shownPitch);
        uiMagic.store (effMagic);
        uiLoopGain.store (gain);
        uiRunaway.store (runLevel);
        uiDive.store (diveLevel);
        uiFrozen.store (freezer.isFrozen());
    }

    // what the panel and Pebble show
    std::atomic<float> uiInput { 0 }, uiOutput { 0 }, uiWet { 0 }, uiPitch { 0 }, uiMagic { 0 },
                       uiLoopGain { 0 }, uiRunaway { 0 }, uiDive { 0 };
    std::atomic<bool> uiFrozen { false };

private:
    double sr = 48000.0;
    DelayLine delay;
    PitchVoice voice1, voice2;
    FreezeLoop freezer;
    Wander chorus1, chorus2;
    Rng noiseRng { 0x2468aceu };
    Biquad toneL, toneR;
    OnePoleLP loopLp;
    DcBlock loopDc;
    Smoother pitchSm, trackSm, primarySm, secondarySm, toneSm, magicSm, inGate, wetGate;
    float diveIn = 0, diveOut = 0, runIn = 0, runOut = 0;
    float diveLevel = 0, runLevel = 0, fb = 0, held = 0, holdCount = 0;
    int tameCount = 0;
    bool primed = false;
    std::atomic<bool> tameRequest { false };
};
} // namespace os
