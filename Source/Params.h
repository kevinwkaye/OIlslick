#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "engine/OilEngine.h"

// Every parameter lives in this one list, the pedal's controls, the footswitch jobs and
// Keeper included, so a host, an automation lane or a remote controller can read and set
// everything the panel and the footswitches can.
namespace ParamIDs
{
// the pedal
inline constexpr const char* pitch     = "pitch";
inline constexpr const char* primary   = "primary";
inline constexpr const char* secondary = "secondary";
inline constexpr const char* tracking  = "tracking";
inline constexpr const char* tone      = "tone";
inline constexpr const char* magic     = "magic";
inline constexpr const char* magicOn   = "magicOn";
inline constexpr const char* bypass    = "bypass";
// extras
inline constexpr const char* version   = "version";
inline constexpr const char* trails    = "trails";
inline constexpr const char* freeze    = "freeze";
inline constexpr const char* jump      = "jump";
inline constexpr const char* dive      = "dive";
inline constexpr const char* runaway   = "runaway";
// expression
inline constexpr const char* expTarget = "expTarget";
inline constexpr const char* expMin    = "expMin";
inline constexpr const char* expMax    = "expMax";
inline constexpr const char* expSmooth = "expSmooth";
// actions for a remote controller: set to 1 to press, 0 to release
inline constexpr const char* foot[4]   = { "footA", "footB", "footC", "footD" };
inline constexpr const char* keeper    = "keeper";
inline constexpr const char* tame      = "tame";
} // namespace ParamIDs

namespace names
{
struct ExpTarget { const char* id; const char* label; };
inline const std::vector<ExpTarget>& expTargets()
{
    static const std::vector<ExpTarget> t {
        { ParamIDs::pitch, "Pitch" }, { ParamIDs::magic, "Magic" }, { ParamIDs::tracking, "Tracking" },
        { ParamIDs::tone, "Tone" }, { ParamIDs::primary, "Primary" }, { ParamIDs::secondary, "Secondary" } };
    return t;
}
enum { ExpPitch = 0, ExpMagic = 1 };

inline juce::String intervalName (int st)
{
    switch (st)
    {
        case -5: return "4th down";
        case -4: return "Maj 3rd down";
        case -3: return "Min 3rd down";
        case -2: return "2nd down";
        case -1: return "Semitone down";
        case 0:  return "Unison";
        case 1:  return "Semitone up";
        case 2:  return "2nd up";
        case 3:  return "Min 3rd up";
        case 4:  return "Maj 3rd up";
        default: return {};
    }
}
} // namespace names

// Heel to toe on Pitch, shaped for playing rather than a straight line: heel is a fourth
// down, toe a third up, a resting zone in the middle holds unison, and each half is drawn
// gently towards whole semitones so slow sweeps land on harmonies and fast ones still glide.
inline float expressionToPitch (float x)
{
    float p;
    if (x < 0.46f) p = os::kPitchMin * (1.0f - x / 0.46f);
    else if (x > 0.54f) p = os::kPitchMax * (x - 0.54f) / 0.46f;
    else p = 0.0f;
    const float r = std::round (p), f = p - r;
    return juce::jlimit (os::kPitchMin, os::kPitchMax, r + f * (0.45f + 0.55f * std::abs (2.0f * f)));
}
inline float pitchToExpression (float st)
{
    return st < 0.0f ? 0.46f * (1.0f + st / -os::kPitchMin) : (st > 0.0f ? 0.54f + 0.46f * st / os::kPitchMax : 0.5f);
}

// Heel to toe on Magic: most of the travel sits where it gets interesting. Heel is a few
// repeats, half travel is pixie trails, the last fifth is the edge of oscillation and beyond.
inline float expressionToMagic (float x)
{
    if (x <= 0.8f) return 0.1f + 0.75f * std::pow (x / 0.8f, 0.8f);
    return 0.85f + 0.15f * (x - 0.8f) / 0.2f;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto add = [&layout] (auto p) { layout.add (std::move (p)); };

    auto pct = [] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; };

    add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::pitch, 1 }, "Pitch",
         NormalisableRange<float> (os::kPitchMin, os::kPitchMax), 0.0f,
         AudioParameterFloatAttributes().withLabel ("st").withStringFromValueFunction ([] (float v, int)
         {
             const String n = names::intervalName (roundToInt (v));
             return (v >= 0.05f ? "+" : (v <= -0.05f ? "-" : "")) + String (std::abs (v), 1) + " st " + n;
         })));
    add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::primary, 1 }, "Primary",
         NormalisableRange<float> (0.0f, 1.0f), 0.72f,
         AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
         {
             if (v < 0.01f) return String ("Off");
             const float db = jmin (6.0f, 40.0f * std::log10 (v / 0.72f));
             return std::abs (db) < 0.3f ? String ("Unity") : (db > 0 ? "+" : "") + String (db, 1) + " dB";
         })));
    add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::secondary, 1 }, "Secondary",
         NormalisableRange<float> (-1.0f, 1.0f), 0.0f,
         AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
         {
             if (std::abs (v) < 0.03f) return String ("Off");
             return String (v > 0 ? "Oct up " : "Oct down ") + String (roundToInt (std::abs (v) * 100.0f)) + "%";
         })));
    add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::tracking, 1 }, "Tracking",
         NormalisableRange<float> (0.0f, 1.0f), 0.55f,
         AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
         {
             const float ms = os::trackingMs (v, os::V2);
             return ms < 12.0f ? String ("Tight") : String (roundToInt (ms)) + " ms";
         })));
    add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::tone, 1 }, "Tone",
         NormalisableRange<float> (0.0f, 1.0f), 0.6f,
         AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
         {
             return String (1.2f * std::pow (16000.0f / 1200.0f, v), 1) + " kHz";
         })));
    add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::magic, 1 }, "Magic",
         NormalisableRange<float> (0.0f, 1.0f), 0.4f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::magicOn, 1 }, "Magic Switch", false));
    add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::bypass, 1 }, "Bypass", false));

    add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::version, 1 }, "Version", StringArray { "V2", "First edition" }, 0));
    add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::trails, 1 }, "Trails", true));
    add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::freeze, 1 }, "Freeze", false));
    add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::jump, 1 }, "Jump", false));
    add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::dive, 1 }, "Dive", false));
    add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::runaway, 1 }, "Runaway", false));

    StringArray targets;
    for (const auto& t : names::expTargets()) targets.add (t.label);
    add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::expTarget, 1 }, "Expression Target", targets, names::ExpPitch));
    add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::expMin, 1 }, "Expression Min",
         NormalisableRange<float> (0.0f, 1.0f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::expMax, 1 }, "Expression Max",
         NormalisableRange<float> (0.0f, 1.0f), 1.0f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::expSmooth, 1 }, "Expression Smoothing",
         NormalisableRange<float> (0.0f, 1.0f), 0.25f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    const char* footNames[4] = { "Footswitch A", "Footswitch B", "Footswitch C", "Footswitch D" };
    for (int i = 0; i < 4; ++i)
        add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::foot[i], 1 }, footNames[i], false));
    add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::keeper, 1 }, "Keeper", false));
    add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::tame, 1 }, "Tame", false));
    return layout;
}
