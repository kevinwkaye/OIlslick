#pragma once

// Small DSP building blocks for OilSlick. Plain C++17, no JUCE, so the engine builds
// anywhere (Windows x64, macOS, Linux ARM64) and can move to a headless host later.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace os
{
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float clampf (float v, float lo, float hi) { return std::min (hi, std::max (lo, v)); }
inline float semitonesToRatio (float st) { return std::exp2 (st / 12.0f); }

// tanh-like soft clipper: unity gain for small signals, never above 1
inline float softClip (float x)
{
    x = clampf (x, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

// Leaves everything under 0.8 alone, then bends smoothly to a ceiling of 1.0.
inline float safetyLimit (float x)
{
    const float a = std::abs (x);
    if (a <= 0.8f) return x;
    const float y = 0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f);
    return x < 0.0f ? -y : y;
}

// One-pole smoother with a time constant in seconds.
struct Smoother
{
    float y = 0.0f, a = 0.0f;
    void setTime (float seconds, double sr) { a = seconds <= 0.0f ? 0.0f : std::exp (-1.0f / (float) (seconds * sr)); }
    void reset (float v) { y = v; }
    float next (float target) { y = target + a * (y - target); return y; }
};

// xorshift noise, -1..1
struct Rng
{
    uint32_t s = 0x9e3779b9u;
    explicit Rng (uint32_t seed = 0x9e3779b9u) : s (seed ? seed : 1u) {}
    float next()
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) (s & 0xffffffu) / 8388608.0f - 1.0f;
    }
};

// Slow random wander, -1..1: a new random target every 1/rate seconds, smoothed.
// This is the pedal's wobble: nothing in it is perfectly steady.
struct Wander
{
    Rng rng;
    Smoother sm;
    float target = 0.0f;
    int countdown = 0, period = 1;
    void prepare (double sr, float rateHz, uint32_t seed)
    {
        rng = Rng (seed);
        period = std::max (1, (int) (sr / std::max (0.01f, rateHz)));
        sm.setTime (0.6f / std::max (0.01f, rateHz), sr);
        sm.reset (0.0f);
        countdown = 0;
    }
    float next()
    {
        if (--countdown <= 0) { target = rng.next(); countdown = period; }
        return sm.next (target);
    }
};

struct OnePoleLP
{
    float y = 0.0f, a = 1.0f;
    void setCutoff (float hz, double sr) { a = 1.0f - std::exp (-kTwoPi * hz / (float) sr); }
    float process (float x) { y += a * (x - y); return y; }
    void reset() { y = 0.0f; }
};

struct DcBlock
{
    float x1 = 0.0f, y1 = 0.0f;
    float process (float x) { const float y = x - x1 + 0.995f * y1; x1 = x; y1 = y; return y; }
    void reset() { x1 = y1 = 0.0f; }
};

// RBJ low-pass, used for Tone
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void setLowPass (float hz, float q, double sr)
    {
        hz = clampf (hz, 20.0f, 0.45f * (float) sr);
        const float w = kTwoPi * hz / (float) sr, c = std::cos (w), al = std::sin (w) / (2.0f * q);
        const float a0 = 1.0f + al;
        b0 = (1.0f - c) * 0.5f / a0; b1 = (1.0f - c) / a0; b2 = b0;
        a1 = -2.0f * c / a0; a2 = (1.0f - al) / a0;
    }
    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0.0f; }
};

// Power-of-two circular delay line with linear interpolation. Linear on purpose:
// the pedal's shifter is grainy, not hi-fi.
struct DelayLine
{
    std::vector<float> buf;
    int mask = 0, w = 0;
    void prepare (int minSize)
    {
        int n = 1;
        while (n < minSize) n <<= 1;
        buf.assign ((size_t) n, 0.0f);
        mask = n - 1;
        w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    void write (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    // delay in samples, at least 1 (the sample just written is delay 1)
    float read (float delay) const
    {
        delay = clampf (delay, 1.0f, (float) (mask - 2));
        const float pos = (float) w - delay;
        const float fl = std::floor (pos);
        const int i0 = (int) fl & mask, i1 = (i0 + 1) & mask;
        const float f = pos - fl;
        return buf[(size_t) i0] + f * (buf[(size_t) i1] - buf[(size_t) i0]);
    }
};
} // namespace os
