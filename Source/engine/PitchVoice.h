#pragma once

#include "DspUtil.h"

namespace os
{
// One pitch-shifting voice: the classic two-head rotating-tap shifter. Two read heads sweep
// through a short window of recent audio at a speed set by the pitch ratio, and each fades
// in and out on a raised-cosine window, half a cycle apart, so one is always loud while the
// other splices.
//
// The splices are deliberately not phase-aligned. That is the pedal's warble: a modern
// shifter hunts for a clean splice point, this one just crossfades wherever it lands.
// The window length also wanders a little, so the splice rhythm never settles.
class PitchVoice
{
public:
    void prepare (double sampleRate, float windowMs, uint32_t seed)
    {
        sr = sampleRate;
        baseWindow = (float) (windowMs * 0.001 * sr);
        line.prepare ((int) (baseWindow * 1.3f) + 16);
        wobble.prepare (sr, 0.7f, seed);
        reset();
    }

    void reset()
    {
        line.clear();
        phase = 0.0f;
    }

    // wobbleDepth: how far the window length wanders (0.08 = 8%)
    float process (float in, float ratio, float wobbleDepth)
    {
        line.write (in);
        const float window = baseWindow * (1.0f + wobbleDepth * wobble.next());
        phase += (1.0f - ratio) / window;
        phase -= std::floor (phase);

        float p2 = phase + 0.5f;
        if (p2 >= 1.0f) p2 -= 1.0f;
        const float d1 = 2.0f + phase * window;
        const float d2 = 2.0f + p2 * window;
        const float g1 = 0.5f - 0.5f * std::cos (kTwoPi * phase);
        return g1 * line.read (d1) + (1.0f - g1) * line.read (d2);
    }

private:
    DelayLine line;
    Wander wobble;
    double sr = 48000.0;
    float baseWindow = 1920.0f;
    float phase = 0.0f;
};

// Freeze: holds the last 1.2 seconds of the wet sound and plays it as a seamless pad.
// Two heads read the held loop half a loop apart, each on a raised-cosine window, so the
// loop's seam is always silent.
class FreezeLoop
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        len = std::max (64, (int) (1.2 * sr));
        history.prepare (len + 8);
        loop.assign ((size_t) len, 0.0f);
        fade.setTime (0.06f, sr);
        fade.reset (0.0f);
        frozen = false;
    }

    void record (float x) { if (! frozen) history.write (x); }

    void engage()
    {
        // copy the last len samples, oldest first
        for (int i = 0; i < len; ++i) loop[(size_t) i] = history.read ((float) (len - i));
        pos = 0;
        frozen = true;
    }

    void release() { frozen = false; }
    bool isFrozen() const { return frozen; }
    bool isSounding() const { return frozen || fade.y > 0.0005f; }

    float play()
    {
        const float level = fade.next (frozen ? 1.0f : 0.0f);
        if (level < 0.0005f) return 0.0f;
        const int half = len / 2;
        const int p1 = pos, p2 = (pos + half) % len;
        const float ph = (float) p1 / (float) len;
        const float g1 = 0.5f - 0.5f * std::cos (kTwoPi * ph);
        const float out = g1 * loop[(size_t) p1] + (1.0f - g1) * loop[(size_t) p2];
        pos = (pos + 1) % len;
        return out * level;
    }

private:
    DelayLine history;
    std::vector<float> loop;
    Smoother fade;
    double sr = 48000.0;
    int len = 57600, pos = 0;
    bool frozen = false;
};
} // namespace os
