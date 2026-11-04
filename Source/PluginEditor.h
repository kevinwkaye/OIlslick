#pragma once

#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>

// Flip Fella palette, shared across all of Kevin's builds.
namespace ff
{
const juce::Colour bg        { 0xff050505 };
const juce::Colour face      { 0xff0d0d0d };
const juce::Colour housing   { 0xff060606 };
const juce::Colour card      { 0xff1b1b1b };
const juce::Colour cardHi    { 0xff252525 };
const juce::Colour cardLow   { 0xff141414 };
const juce::Colour ink       { 0xfff3f0e8 };
const juce::Colour dim       { 0xffa29e95 };
const juce::Colour faint     { 0xff5a5852 };
const juce::Colour line      { 0xff3a3a3a };
const juce::Colour ring      { 0xff1a1a1a };
const juce::Colour pin       { 0xff3c3c3c };
const juce::Colour red       { 0xffe8392f };
const juce::Colour teal      { 0xff3fd6c8 };
const juce::Colour coral     { 0xffff7a59 };
const juce::Colour amber     { 0xfff2c230 };
const juce::Colour green     { 0xff4fcf6a };
const juce::Colour violet    { 0xffb06bff };
const juce::Colour blob      { 0xff4d5bff };
const juce::Colour blobShade { 0xff2329a8 };
const juce::Colour eye       { 0xffffffff };
const juce::Colour faceInk   { 0xff0b0b12 };

// the oil-slick sheen round Pebble
inline juce::Colour slick (float t)
{
    static const juce::Colour c[6] = { teal, blob, violet, coral, amber, green };
    t = t - std::floor (t);
    const float x = t * 6.0f;
    const int i = (int) x % 6;
    return c[i].interpolatedWith (c[(i + 1) % 6], x - std::floor (x));
}

juce::Font font (float height, float kerning = 0.04f);
void label (juce::Graphics&, const juce::String& text, juce::Rectangle<int> r,
            juce::Justification j = juce::Justification::centredLeft, juce::Colour c = dim, float size = 13.0f);
} // namespace ff

namespace ui
{
void pill (juce::Graphics&, juce::Rectangle<float> b, const juce::String& text, bool on, float size, juce::Colour onColour = ff::ink);

// A split-flap card that flips like the Karlsson clock when its value changes.
class FlipCard : public juce::Component
{
public:
    void setValue (const juce::String& v);
    void setTextHeight (float h) { textHeight = h; }
    void advance (double dt);
    void paint (juce::Graphics&) override;

private:
    void drawHalf (juce::Graphics&, juce::Rectangle<float> card, const juce::String& text, bool top, float scale, bool shade);
    juce::String current, previous;
    float progress = 1.0f;
    float textHeight = 34.0f;
};

// Rounded pill buttons bound to a choice parameter.
class Pills : public juce::Component
{
public:
    Pills (juce::AudioProcessorValueTreeState&, const juce::String& paramId, juce::StringArray labels, float textSize = 13.0f);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> cell (int i) const;
    juce::StringArray labels;
    int selected = 0;
    float textSize;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// A single pill that turns ink-white when its switch is on.
class ToggleChip : public juce::Component
{
public:
    ToggleChip (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& text,
                const juce::String& onText = {}, float textSize = 13.0f, juce::Colour onColour = ff::ink);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::String text, onText;
    float textSize;
    juce::Colour onColour;
    bool on = false;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// A plain pill button that runs a function.
class ActionChip : public juce::Component
{
public:
    ActionChip (const juce::String& t, std::function<void()> fn, float textSize = 12.0f)
        : text (t), action (std::move (fn)), size (textSize) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics& g) override { pill (g, getLocalBounds().toFloat().reduced (1.0f), text, lit, size, colour); }
    void mouseDown (const juce::MouseEvent&) override { if (action) action(); }
    void setLit (bool l) { if (l != lit) { lit = l; repaint(); } }
    void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
    void setColour (juce::Colour c) { colour = c; repaint(); }

private:
    juce::String text;
    std::function<void()> action;
    float size;
    bool lit = false;
    juce::Colour colour = ff::ink;
};

class FlipLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
};

// A knob drawn as a small clock face, with its name above and its value below.
class Dial : public juce::Component
{
public:
    Dial (OilSlickProcessor&, const juce::String& paramId, const juce::String& caption, const juce::String& hint,
          bool redHand = false, int knobSize = 80);
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<juce::String()> valueText;   // overrides the parameter's own text
    juce::Slider slider;

private:
    juce::RangedAudioParameter* param;
    juce::String caption, hint;
    int knobSize;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class LabelledSlider : public juce::Component
{
public:
    LabelledSlider (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Slider slider;
    juce::RangedAudioParameter* param;
    juce::String caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// The hero: Pitch on a Karlsson face, unison at noon, a fourth down to a third up.
// The hand shows what you hear (Jump, Dive and expression included).
class PitchDial : public juce::Component
{
public:
    explicit PitchDial (OilSlickProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

private:
    OilSlickProcessor& proc;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    float shown = 0.0f;
};

// Magic, with a red hand and an arc showing where the runaway has taken it.
class MagicDial : public juce::Component
{
public:
    explicit MagicDial (OilSlickProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

private:
    OilSlickProcessor& proc;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    float eff = 0.0f;
};

// How hard the Magic loop is feeding itself, from repeats to burn.
class EdgeMeter : public juce::Component
{
public:
    explicit EdgeMeter (OilSlickProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void tick();
    static juce::String zoneName (float magic);

private:
    OilSlickProcessor& proc;
    float eff = 0.0f;
};

// A big press-and-hold button that runs a function on press and on release.
class HoldButton : public juce::Component
{
public:
    std::function<void()> onDown, onUp;
    void set (const juce::String& text, bool lit, juce::Colour litColour);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { pressed = true; repaint(); if (onDown) onDown(); }
    void mouseUp (const juce::MouseEvent&) override { pressed = false; repaint(); if (onUp) onUp(); }

private:
    juce::String text;
    bool lit = false, pressed = false;
    juce::Colour litColour = ff::coral;
};

// One footswitch in the bottom strip. Works exactly like the real one: tap, hold, pairs.
class FootButton : public juce::Component
{
public:
    FootButton (OilSlickProcessor& p, int s) : proc (p), sw (s) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { proc.footDown (sw); }
    void mouseUp (const juce::MouseEvent&) override { proc.footUp (sw); }
    void tick();

private:
    OilSlickProcessor& proc;
    int sw;
    juce::String big, small;
    juce::Colour led = ff::pin;
    bool lit = false, down = false;
};

// The expression pedal: live position, a Pitch or Magic toggle, and a menu for the rest.
class ExpressionBar : public juce::Component
{
public:
    explicit ExpressionBar (OilSlickProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick();
    std::function<void()> showPedalPage;

private:
    OilSlickProcessor& proc;
    ActionChip pitchChip, magicChip;
    juce::RangedAudioParameter* target;
    float level = 0.0f;
    int lastTarget = -1;
};

// "FOOTSWITCHES": a dot that lights when the footswitch is heard directly, and the input menu.
class FootInputLabel : public juce::Component
{
public:
    explicit FootInputLabel (OilSlickProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    OilSlickProcessor& proc;
};

// One footswitch on the Pedal page: its jobs, what it listens for, Learn and Defaults.
class FootswitchCard : public juce::Component
{
public:
    FootswitchCard (OilSlickProcessor&, int sw);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick (double dt);

private:
    OilSlickProcessor& proc;
    int sw;
    FlipCard letter;
    ActionChip learn, reset;
    juce::String sends;
};

// Pebble, reacting to the pitch, the Magic loop, the footswitches and the expression pedal.
class Pebble : public juce::Component
{
public:
    explicit Pebble (OilSlickProcessor& p) : proc (p) {}
    void update (double t, double dt);
    void paint (juce::Graphics&) override;
    juce::String mood() const;
    juce::String hint() const;

private:
    OilSlickProcessor& proc;
    double now = 0.0;
    float pitch = 0.0f, magic = 0.0f, wild = 0.0f, level = 0.0f, lean = 0.0f;
    float hop = 0.0f, nod = 0.0f, calm = 0.0f, ringAngle = 0.0f;
    bool asleep = false, frozen = false, blinking = false;
    double nextBlink = 2.0, blinkUntil = 0.0;
    int lastFoot = 0, lastKeep = 0, lastTame = 0;
};

class OilSlickEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit OilSlickEditor (OilSlickProcessor&);
    ~OilSlickEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override { proc.focusHere(); }
    void setPage (int p);

private:
    void timerCallback() override;
    void keeperMenu();
    void closeNote (bool save);
    void section (juce::Graphics&, juce::Rectangle<int>) const;

    OilSlickProcessor& proc;
    FlipLookAndFeel lnf;
    int page = 0;   // 0 Main, 1 Pedal

    // header
    Pills version;
    ToggleChip trails;
    ActionChip mainTab, pedalTab, keepButton, keepMenu;
    FootInputLabel footInput;

    // main
    PitchDial pitchDial;
    FlipCard pitchCard;
    Dial primary, secondary, tracking, tone;
    MagicDial magicDial;
    ToggleChip magicSwitch;
    EdgeMeter edge;
    ActionChip tameButton;
    HoldButton runButton;
    Pebble pebble;
    FlipCard moodCard;

    // strip
    std::array<std::unique_ptr<FootButton>, 4> feet;
    ExpressionBar expression;

    // pedal page
    std::array<std::unique_ptr<FootswitchCard>, 4> cards;
    std::array<std::unique_ptr<ActionChip>, 6> targetChips;
    LabelledSlider expMin, expMax, expSmooth;
    ActionChip calibrate, learnExp, curve, liveChip, learnKeep, exportKeep, folderKeep;

    juce::TextEditor noteEditor;
    int noteNumber = 0;
    double noteUntil = 0.0;

    double lastTick = 0.0, start = 0.0;
};
} // namespace ui

using OilSlickEditor = ui::OilSlickEditor;
