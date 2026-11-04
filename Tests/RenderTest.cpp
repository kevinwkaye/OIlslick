// Offline engine test: no plugin host, no JUCE. Plays a short phrase through every
// scenario, checks for NaNs, runaway levels and dead output, checks that the Magic runaway
// really self-oscillates and that Tame and Freeze do their jobs, then measures CPU per
// instance at 48 kHz with 64 and 128 sample buffers (a house rule for every release).
//
// Pass a folder to also write a WAV of each scenario.

#include "OilEngine.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace os;

namespace
{
struct Scene
{
    std::string name;
    EngineParams p;
    double seconds = 8.0;
    double playUntil = 5.0;   // input stops here
    double tameAt = -1.0;     // press Tame (A+B) here
    std::function<void (double t, EngineParams&)> script;   // changes over time
    std::function<bool (const std::vector<float>&, double sr, std::string& why)> check;
};

// a strummed chord with a pick-like attack, retriggered every 1.25 s
float phrase (double t)
{
    static const double freqs[4] = { 110.0, 164.81, 220.0, 277.18 };
    const double local = std::fmod (t, 1.25);
    const double env = std::exp (-local * 2.6) * std::min (1.0, local * 400.0);
    double s = 0.0;
    for (int k = 0; k < 4; ++k)
    {
        const double ph = 2.0 * 3.14159265358979 * freqs[k] * t;
        s += std::sin (ph) + 0.35 * std::sin (2.0 * ph) + 0.15 * std::sin (3.0 * ph);
    }
    return (float) (0.12 * env * s);
}

double rms (const std::vector<float>& x, double sr, double from, double to)
{
    const size_t a = (size_t) (from * sr), b = std::min (x.size(), (size_t) (to * sr));
    double s = 0.0;
    for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i];
    return b > a ? std::sqrt (s / (double) (b - a)) : 0.0;
}

void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int sr)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    const uint32_t frames = (uint32_t) l.size(), dataBytes = frames * 4;
    auto u32 = [f] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto u16 = [f] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); u32 (36 + dataBytes); std::fwrite ("WAVEfmt ", 1, 8, f);
    u32 (16); u16 (1); u16 (2); u32 ((uint32_t) sr); u32 ((uint32_t) sr * 4); u16 (4); u16 (16);
    std::fwrite ("data", 1, 4, f); u32 (dataBytes);
    for (uint32_t i = 0; i < frames; ++i)
    {
        const int16_t a = (int16_t) std::lround (clampf (l[i], -1.0f, 1.0f) * 32767.0f);
        const int16_t b = (int16_t) std::lround (clampf (r[i], -1.0f, 1.0f) * 32767.0f);
        std::fwrite (&a, 2, 1, f); std::fwrite (&b, 2, 1, f);
    }
    std::fclose (f);
}

bool run (const Scene& s, double sr, const std::string& wavDir, std::string& why)
{
    auto eng = std::make_unique<OilEngine>();
    eng->prepare (sr);
    const int block = 128;
    const size_t total = (size_t) (s.seconds * sr);
    std::vector<float> outL (total), outR (total), wet (total);
    std::vector<float> bl (block), br (block);
    EngineParams p = s.p;
    for (size_t pos = 0; pos < total; pos += block)
    {
        const int n = (int) std::min ((size_t) block, total - pos);
        const double t = (double) pos / sr;
        if (s.script) s.script (t, p);
        if (s.tameAt >= 0.0 && t <= s.tameAt && t + block / sr > s.tameAt) eng->tame();
        for (int i = 0; i < n; ++i)
        {
            const double ti = (double) (pos + (size_t) i) / sr;
            bl[(size_t) i] = br[(size_t) i] = ti < s.playUntil ? phrase (ti) : 0.0f;
        }
        std::vector<float> dry (bl.begin(), bl.begin() + n);
        eng->process (bl.data(), br.data(), bl.data(), br.data(), n, p);
        for (int i = 0; i < n; ++i)
        {
            outL[pos + (size_t) i] = bl[(size_t) i];
            outR[pos + (size_t) i] = br[(size_t) i];
            wet[pos + (size_t) i] = bl[(size_t) i] - dry[(size_t) i];
        }
    }
    for (size_t i = 0; i < total; ++i)
    {
        if (! std::isfinite (outL[i]) || ! std::isfinite (outR[i])) { why = "NaN or infinity at sample " + std::to_string (i); return false; }
        if (std::abs (outL[i]) > 1.0001f || std::abs (outR[i]) > 1.0001f) { why = "level above full scale"; return false; }
    }
    if (! wavDir.empty() && sr == 48000.0) writeWav (wavDir + "/" + s.name + ".wav", outL, outR, (int) sr);
    return s.check ? s.check (wet, sr, why) : true;
}

double cpuPercent (int block, int version)
{
    const double sr = 48000.0;
    OilEngine eng;
    eng.prepare (sr);
    EngineParams p;
    p.secondary = 0.6f; p.magicOn = true; p.magic = 0.7f; p.pitch = 3.0f; p.version = version;
    const int seconds = 20;
    std::vector<float> l ((size_t) block), r ((size_t) block);
    const auto start = std::chrono::steady_clock::now();
    for (long pos = 0; pos < (long) (seconds * sr); pos += block)
    {
        for (int i = 0; i < block; ++i) l[(size_t) i] = r[(size_t) i] = phrase ((double) (pos + i) / sr);
        eng.process (l.data(), r.data(), l.data(), r.data(), block, p);
    }
    const double used = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    return 100.0 * used / (double) seconds;
}
} // namespace

int main (int argc, char** argv)
{
    const std::string wavDir = argc > 1 ? argv[1] : "";
    std::vector<Scene> scenes;
    auto hasWet = [] (double minRms)
    {
        return [minRms] (const std::vector<float>& w, double sr, std::string& why)
        {
            const double r = rms (w, sr, 0.5, 4.5);
            if (r < minRms) { why = "wet signal too quiet: " + std::to_string (r); return false; }
            return true;
        };
    };

    { Scene s; s.name = "01_unison_chorus"; s.check = hasWet (0.01); scenes.push_back (s); }
    { Scene s; s.name = "02_third_up"; s.p.pitch = 4.0f; s.check = hasWet (0.01); scenes.push_back (s); }
    { Scene s; s.name = "03_fourth_down"; s.p.pitch = -5.0f; s.check = hasWet (0.01); scenes.push_back (s); }
    { Scene s; s.name = "04_secondary_up"; s.p.secondary = 0.8f; s.p.pitch = 2.0f; s.check = hasWet (0.01); scenes.push_back (s); }
    { Scene s; s.name = "05_secondary_down"; s.p.secondary = -0.8f; s.p.pitch = -2.0f; s.check = hasWet (0.01); scenes.push_back (s); }
    { Scene s; s.name = "06_tracking_long"; s.p.tracking = 0.0f; s.check = hasWet (0.005); scenes.push_back (s); }
    { Scene s; s.name = "07_tracking_tight"; s.p.tracking = 1.0f; s.check = hasWet (0.01); scenes.push_back (s); }
    { Scene s; s.name = "08_tone_dark"; s.p.tone = 0.0f; s.check = hasWet (0.003); scenes.push_back (s); }
    { Scene s; s.name = "09_magic_repeats"; s.p.magicOn = true; s.p.magic = 0.3f; s.p.tracking = 0.4f; s.check = hasWet (0.01); scenes.push_back (s); }
    { Scene s; s.name = "10_magic_pixie_climb"; s.p.magicOn = true; s.p.magic = 0.7f; s.p.pitch = 2.0f; s.p.tracking = 0.5f; scenes.push_back (s); }
    { Scene s; s.name = "11_magic_pixie_fall"; s.p.magicOn = true; s.p.magic = 0.7f; s.p.pitch = -1.0f; s.p.tracking = 0.5f; scenes.push_back (s); }
    { Scene s; s.name = "12_magic_full_burn"; s.p.magicOn = true; s.p.magic = 1.0f; s.p.secondary = 0.5f; scenes.push_back (s); }
    {
        // the knob's Magic does nothing with both voices down, as on the pedal
        Scene s; s.name = "13_magic_needs_a_voice"; s.p.magicOn = true; s.p.magic = 1.0f; s.p.primary = 0.0f; s.p.secondary = 0.0f;
        s.check = [] (const std::vector<float>& w, double sr, std::string& why)
        { const double r = rms (w, sr, 5.5, 8.0); if (r > 0.002) { why = "Magic sounded with no voice up: " + std::to_string (r); return false; } return true; };
        scenes.push_back (s);
    }
    {
        // hold B: the loop must take off on its own once the playing stops
        Scene s; s.name = "14_runaway_oscillates"; s.seconds = 10.0; s.playUntil = 1.5; s.p.magic = 0.3f; s.p.tracking = 0.6f;
        s.script = [] (double t, EngineParams& p) { p.runaway = t > 1.0 && t < 8.0; };
        s.check = [] (const std::vector<float>& w, double sr, std::string& why)
        {
            const double during = rms (w, sr, 6.0, 7.8), after = rms (w, sr, 9.5, 10.0);
            if (during < 0.05) { why = "runaway didn't self-oscillate: " + std::to_string (during); return false; }
            if (after > during * 0.5) { why = "runaway didn't settle after release"; return false; }
            return true;
        };
        scenes.push_back (s);
    }
    {
        Scene s; s.name = "15_tame_stops_it"; s.seconds = 8.0; s.playUntil = 1.5; s.p.magicOn = true; s.p.magic = 1.0f;
        s.script = [] (double t, EngineParams& p) { p.runaway = t > 1.0 && t < 6.0; };
        s.tameAt = 4.0;   // Tame while B is still held: it must still go quiet, then may build again
        s.check = [] (const std::vector<float>& w, double sr, std::string& why)
        {
            const double before = rms (w, sr, 3.5, 3.95), after = rms (w, sr, 4.05, 4.25);
            if (before < 0.05) { why = "nothing to tame: " + std::to_string (before); return false; }
            if (after > before * 0.1) { why = "Tame didn't cut the feedback: " + std::to_string (after); return false; }
            return true;
        };
        scenes.push_back (s);
    }
    {
        Scene s; s.name = "16_freeze_holds"; s.seconds = 8.0; s.playUntil = 3.0; s.p.magicOn = true; s.p.magic = 0.4f;
        s.script = [] (double t, EngineParams& p) { p.freeze = t > 2.5; };
        s.check = [] (const std::vector<float>& w, double sr, std::string& why)
        {
            const double held = rms (w, sr, 6.0, 8.0);
            if (held < 0.01) { why = "freeze didn't hold the sound: " + std::to_string (held); return false; }
            return true;
        };
        scenes.push_back (s);
    }
    {
        Scene s; s.name = "17_bypass_cuts"; s.p.trails = false;
        s.script = [] (double t, EngineParams& p) { p.active = t < 3.0; };
        s.check = [] (const std::vector<float>& w, double sr, std::string& why)
        { const double r = rms (w, sr, 4.0, 8.0); if (r > 0.001) { why = "wet still sounding after bypass: " + std::to_string (r); return false; } return true; };
        scenes.push_back (s);
    }
    {
        Scene s; s.name = "18_bypass_trails"; s.p.magicOn = true; s.p.magic = 0.95f;
        s.script = [] (double t, EngineParams& p) { p.active = t < 3.0; };
        s.check = [] (const std::vector<float>& w, double sr, std::string& why)
        { const double r = rms (w, sr, 7.0, 8.0); if (r > 0.01) { why = "bypassed trails didn't die away: " + std::to_string (r); return false; } return true; };
        scenes.push_back (s);
    }
    {
        Scene s; s.name = "19_jump_and_dive"; s.p.magicOn = true; s.p.magic = 0.5f;
        s.script = [] (double t, EngineParams& p) { p.jump = t > 1.0 && t < 2.5; p.dive = t > 3.0 && t < 4.5; };
        s.check = hasWet (0.01);
        scenes.push_back (s);
    }
    {
        Scene s; s.name = "20_tracking_sweep_warp"; s.p.magicOn = true; s.p.magic = 0.9f;
        s.script = [] (double t, EngineParams& p) { p.tracking = (float) (0.5 + 0.45 * std::sin (t * 2.0)); };
        scenes.push_back (s);
    }
    { Scene s; s.name = "21_first_edition"; s.p.version = FirstEdition; s.p.magicOn = true; s.p.magic = 0.6f; s.check = hasWet (0.004); scenes.push_back (s); }
    {
        Scene s; s.name = "22_first_edition_runaway"; s.p.version = FirstEdition; s.seconds = 8.0; s.playUntil = 1.0;
        s.script = [] (double t, EngineParams& p) { p.runaway = t > 0.5 && t < 6.0; };
        scenes.push_back (s);
    }

    int failed = 0;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (const auto& s : scenes)
        {
            std::string why;
            const bool ok = run (s, sr, wavDir, why);
            if (! ok) ++failed;
            if (! ok || sr == 48000.0)
                std::printf ("%-28s %6.0f Hz  %s %s\n", s.name.c_str(), sr, ok ? "ok" : "FAILED", why.c_str());
        }

    std::printf ("\nCPU per instance at 48 kHz (one core, Magic and both voices on):\n");
    for (int version : { V2, FirstEdition })
        for (int block : { 64, 128 })
            std::printf ("  %-14s %3d samples: %.2f%%\n", version == V2 ? "V2" : "First edition", block, cpuPercent (block, version));

    std::printf ("\n%s: %d failed\n", failed ? "FAILED" : "PASSED", failed);
    return failed ? 1 : 0;
}
