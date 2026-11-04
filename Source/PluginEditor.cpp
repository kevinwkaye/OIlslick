#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;

// =====================================================================================
// Palette helpers
// =====================================================================================
namespace ff
{
Font font (float height, float kerning)
{
    static Typeface::Ptr typeface = Typeface::createSystemTypefaceFor (BinaryData::RobotoCondensedBold_ttf,
                                                                       BinaryData::RobotoCondensedBold_ttfSize);
    return Font (FontOptions (typeface).withHeight (height).withKerningFactor (kerning));
}

void label (Graphics& g, const String& text, Rectangle<int> r, Justification j, Colour c, float size)
{
    g.setColour (c);
    g.setFont (font (size, 0.08f));
    g.drawText (text, r, j, false);
}
} // namespace ff

namespace ui
{
static double nowSeconds() { return Time::getMillisecondCounterHiRes() * 0.001; }
static float raw (OilSlickProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); }
static bool isOn (OilSlickProcessor& p, const char* id) { return raw (p, id) > 0.5f; }
static const String dot = String::fromUTF8 (" \xc2\xb7 ");

static void setParam (RangedAudioParameter* p, float plain)
{
    if (p == nullptr) return;
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (plain));
    p->endChangeGesture();
}

static String signedSemis (float v)
{
    return (v >= 0.05f ? "+" : (v <= -0.05f ? String::fromUTF8 ("\xe2\x88\x92") : String())) + String (std::abs (v), 1);
}

void pill (Graphics& g, Rectangle<float> b, const String& text, bool on, float size, Colour onColour)
{
    const float r = b.getHeight() * 0.5f;
    if (on) { g.setColour (onColour); g.fillRoundedRectangle (b, r); }
    else { g.setColour (ff::line); g.drawRoundedRectangle (b.reduced (1.0f), r, 2.0f); }
    g.setColour (on ? ff::bg : ff::ink);
    g.setFont (ff::font (size, 0.06f));
    g.drawText (text, b, Justification::centred, false);
}

// =====================================================================================
// Flip card
// =====================================================================================
void FlipCard::setValue (const String& v)
{
    if (v == current) return;
    previous = current;
    current = v;
    progress = previous.isEmpty() ? 1.0f : 0.0f;
    repaint();
}

void FlipCard::advance (double dt)
{
    if (progress < 1.0f)
    {
        progress = jmin (1.0f, progress + (float) (dt / 0.26));
        repaint();
    }
}

void FlipCard::drawHalf (Graphics& g, Rectangle<float> card, const String& text, bool top, float scale, bool shade)
{
    const float hinge = card.getCentreY();
    const float halfH = card.getHeight() * 0.5f;
    if (scale <= 0.001f) return;

    Graphics::ScopedSaveState save (g);
    auto clip = top ? Rectangle<float> (card.getX(), hinge - halfH * scale, card.getWidth(), halfH * scale)
                    : Rectangle<float> (card.getX(), hinge, card.getWidth(), halfH * scale);
    g.reduceClipRegion (clip.getSmallestIntegerContainer());
    g.addTransform (AffineTransform::scale (1.0f, scale, 0.0f, hinge));

    if (top) g.setGradientFill (ColourGradient (ff::cardHi, 0, card.getY(), ff::card, 0, hinge, false));
    else     g.setGradientFill (ColourGradient (ff::card, 0, hinge, ff::cardLow, 0, card.getBottom(), false));
    g.fillRoundedRectangle (card, 7.0f);
    g.setColour (ff::ink);
    g.setFont (ff::font (textHeight, 0.02f));
    g.drawFittedText (text, card.reduced (6.0f, 0.0f).toNearestInt(), Justification::centred, 1, 0.7f);

    if (shade)
    {
        g.setColour (Colours::black.withAlpha (0.45f * (1.0f - scale)));
        g.fillRoundedRectangle (card, 7.0f);
    }
}

void FlipCard::paint (Graphics& g)
{
    auto outer = getLocalBounds().toFloat();
    g.setColour (ff::housing);
    g.fillRoundedRectangle (outer, 10.0f);
    auto card = outer.reduced (6.0f);

    if (progress >= 1.0f)
    {
        drawHalf (g, card, current, true, 1.0f, false);
        drawHalf (g, card, current, false, 1.0f, false);
    }
    else
    {
        drawHalf (g, card, current, true, 1.0f, false);
        drawHalf (g, card, previous, false, 1.0f, false);
        const float eased = 1.0f - std::pow (1.0f - progress, 2.0f);
        if (eased < 0.5f) drawHalf (g, card, previous, true, 1.0f - eased * 2.0f, true);
        else              drawHalf (g, card, current, false, (eased - 0.5f) * 2.0f, true);
    }

    g.setColour (Colours::black);
    g.fillRect (card.getX(), card.getCentreY() - 1.5f, card.getWidth(), 3.0f);
    g.setColour (ff::pin);
    g.fillRoundedRectangle (outer.getX() + 1.0f, card.getCentreY() - 6.0f, 3.0f, 12.0f, 1.0f);
    g.fillRoundedRectangle (outer.getRight() - 4.0f, card.getCentreY() - 6.0f, 3.0f, 12.0f, 1.0f);
}

// =====================================================================================
// Pills and chips
// =====================================================================================
Pills::Pills (AudioProcessorValueTreeState& s, const String& paramId, StringArray l, float size)
    : labels (std::move (l)), textSize (size)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    attachment = std::make_unique<ParameterAttachment> (*s.getParameter (paramId), [this] (float v) { selected = (int) std::lround (v); repaint(); });
    attachment->sendInitialUpdate();
}

Rectangle<float> Pills::cell (int i) const
{
    const int n = labels.size();
    const float gap = 8.0f, w = ((float) getWidth() - gap * (float) (n - 1)) / (float) n;
    return { (float) i * (w + gap), 0.0f, w, (float) getHeight() };
}

void Pills::paint (Graphics& g)
{
    for (int i = 0; i < labels.size(); ++i) pill (g, cell (i).reduced (1.0f), labels[i], i == selected, textSize);
}

void Pills::mouseDown (const MouseEvent& e)
{
    for (int i = 0; i < labels.size(); ++i)
        if (cell (i).contains (e.position)) { attachment->setValueAsCompleteGesture ((float) i); return; }
}

ToggleChip::ToggleChip (AudioProcessorValueTreeState& s, const String& paramId, const String& t, const String& tOn, float size, Colour c)
    : text (t), onText (tOn.isEmpty() ? t : tOn), textSize (size), onColour (c)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    attachment = std::make_unique<ParameterAttachment> (*s.getParameter (paramId), [this] (float v) { on = v > 0.5f; repaint(); });
    attachment->sendInitialUpdate();
}

void ToggleChip::paint (Graphics& g) { pill (g, getLocalBounds().toFloat().reduced (1.0f), on ? onText : text, on, textSize, onColour); }
void ToggleChip::mouseDown (const MouseEvent&) { attachment->setValueAsCompleteGesture (on ? 0.0f : 1.0f); }

// =====================================================================================
// Look and feel
// =====================================================================================
void FlipLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle, Slider& s)
{
    const float cy = (float) y + (float) h * 0.5f;
    const float left = (float) x + 7.0f, right = (float) (x + w) - 7.0f;
    g.setColour (ff::ring);
    g.fillRoundedRectangle (left, cy - 3.0f, right - left, 6.0f, 3.0f);
    g.setColour (ff::ink.withAlpha (s.isMouseOverOrDragging() ? 1.0f : 0.85f));
    g.fillRoundedRectangle (left, cy - 3.0f, jmax (0.0f, pos - left), 6.0f, 3.0f);
    g.setColour (ff::ink);
    g.fillEllipse (pos - 7.0f, cy - 7.0f, 14.0f, 14.0f);
}

void FlipLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    if (s.getProperties()["hidden"]) return;   // the hero dials draw themselves
    const float size = (float) jmin (w, h);
    const Point<float> c ((float) x + (float) w * 0.5f, (float) y + (float) h * 0.5f);
    const float r = size * 0.5f - 1.0f;

    g.setColour (ff::ring);
    g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - r * 0.9f, c.y - r * 0.9f, r * 1.8f, r * 1.8f);

    g.setColour (ff::ink);
    for (int i = 0; i <= 10; ++i)
    {
        const float a = start + (end - start) * (float) i / 10.0f;
        g.drawLine ({ { c.x + std::sin (a) * r * 0.80f, c.y - std::cos (a) * r * 0.80f },
                      { c.x + std::sin (a) * r * 0.92f, c.y - std::cos (a) * r * 0.92f } }, 2.0f);
    }

    const bool red = s.getProperties()["red"];
    g.setColour (red ? ff::red : ff::ink);
    const float a = start + pos * (end - start);
    Path hand;
    hand.addRoundedRectangle (-3.0f, -r * 0.68f, 6.0f, r * 0.68f + r * 0.15f, 2.0f);
    g.fillPath (hand, AffineTransform::rotation (a).translated (c));
    g.fillEllipse (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - 1.8f, c.y - 1.8f, 3.6f, 3.6f);
}

static void setupRotary (Slider& s)
{
    s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    s.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    s.setMouseCursor (MouseCursor::PointingHandCursor);
}

// =====================================================================================
// Dials and sliders
// =====================================================================================
Dial::Dial (OilSlickProcessor& p, const String& paramId, const String& c, const String& h, bool redHand, int size)
    : caption (c), hint (h), knobSize (size)
{
    param = p.apvts.getParameter (paramId);
    setupRotary (slider);
    slider.getProperties().set ("red", redHand);
    slider.onValueChange = [this] { repaint(); };
    slider.onDragStart = [&p] { p.focusHere(); };
    addAndMakeVisible (slider);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.apvts, paramId, slider);
    slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
}

void Dial::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedTop (20).withTrimmedBottom (38).withSizeKeepingCentre (knobSize, knobSize));
}

void Dial::paint (Graphics& g)
{
    auto r = getLocalBounds();
    ff::label (g, caption.toUpperCase(), r.removeFromTop (18), Justification::centred, ff::dim);
    ff::label (g, hint.toUpperCase(), r.removeFromBottom (14), Justification::centred, ff::dim, 10.5f);
    const String value = valueText ? valueText() : param->getCurrentValueAsText();
    ff::label (g, value.toUpperCase(), r.removeFromBottom (22), Justification::centred, ff::ink, 15.0f);
}

LabelledSlider::LabelledSlider (AudioProcessorValueTreeState& s, const String& paramId, const String& c) : caption (c)
{
    param = s.getParameter (paramId);
    slider.setSliderStyle (Slider::LinearHorizontal);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setMouseCursor (MouseCursor::PointingHandCursor);
    addAndMakeVisible (slider);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, paramId, slider);
    slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
    slider.onValueChange = [this] { repaint(); };
}

void LabelledSlider::resized() { slider.setBounds (getLocalBounds().withTrimmedTop (16)); }

void LabelledSlider::paint (Graphics& g)
{
    auto top = getLocalBounds().removeFromTop (16);
    ff::label (g, caption.toUpperCase(), top, Justification::centredLeft, ff::dim, 11.0f);
    ff::label (g, param->getCurrentValueAsText().toUpperCase(), top, Justification::centredRight, ff::ink, 11.0f);
}

// =====================================================================================
// Pitch: the hero dial. Unison at noon, so the two sides are spread unevenly:
// five semitones down to the left, four up to the right.
// =====================================================================================
static float pitchAngle (float st) // radians from noon, clockwise
{
    const float deg = st < 0.0f ? st / -os::kPitchMin * 135.0f : st / os::kPitchMax * 135.0f;
    return degreesToRadians (deg);
}

PitchDial::PitchDial (OilSlickProcessor& p) : proc (p)
{
    setupRotary (slider);
    slider.getProperties().set ("hidden", true);
    slider.onDragStart = [&p] { p.focusHere(); };
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.apvts, ParamIDs::pitch, slider);
    slider.setDoubleClickReturnValue (true, 0.0);
    // the knob's travel follows the face: noon is unison
    slider.setRotaryParameters (MathConstants<float>::twoPi - degreesToRadians (135.0f), MathConstants<float>::twoPi + degreesToRadians (135.0f), true);
}

void PitchDial::resized()
{
    const auto c = Point<int> (getWidth() / 2, (int) (getHeight() * 0.54f));
    slider.setBounds (Rectangle<int> (240, 240).withCentre (c));
    // map the slider's linear position onto the uneven face
    slider.setSkewFactorFromMidPoint (0.0);
}

void PitchDial::tick()
{
    const float target = proc.getEngine().uiPitch.load();
    if (std::abs (target - shown) > 0.004f) { shown = target; repaint(); }
}

void PitchDial::paint (Graphics& g)
{
    const Point<float> c ((float) getWidth() * 0.5f, (float) getHeight() * 0.54f);
    const float r = 108.0f;
    g.setColour (ff::ring);
    g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - r * 0.9f, c.y - r * 0.9f, r * 1.8f, r * 1.8f);

    // a tick for every semitone
    g.setColour (ff::ink);
    for (int st = (int) os::kPitchMin; st <= (int) os::kPitchMax; ++st)
    {
        const float a = pitchAngle ((float) st);
        const float in = st == 0 ? 0.70f : 0.80f;
        g.drawLine ({ { c.x + std::sin (a) * r * in, c.y - std::cos (a) * r * in },
                      { c.x + std::sin (a) * r * 0.93f, c.y - std::cos (a) * r * 0.93f } }, st == 0 ? 4.0f : 3.0f);
    }

    // labels round the outside
    struct L { int st; const char* t; };
    static const L labels[] = { { -5, "4TH" }, { -4, "3RD" }, { -2, "2ND" }, { 0, "UNISON" }, { 2, "2ND" }, { 4, "3RD" } };
    const String down = String::fromUTF8 (" \xe2\x86\x93"), up = String::fromUTF8 (" \xe2\x86\x91");
    for (const auto& l : labels)
    {
        const float a = pitchAngle ((float) l.st);
        const Point<float> p (c.x + std::sin (a) * (r + 40.0f), c.y - std::cos (a) * (r + 30.0f));
        const String text = String (l.t) + (l.st < 0 ? down : (l.st > 0 ? up : String()));
        ff::label (g, text, Rectangle<float> (70.0f, 18.0f).withCentre (p).toNearestInt(), Justification::centred,
                   std::abs (shown - (float) l.st) < 0.5f ? ff::ink : ff::dim, 12.0f);
    }

    // an arc from noon to what you hear: teal going up, coral going down
    if (std::abs (shown) > 0.03f)
    {
        Path arc;
        arc.addCentredArc (c.x, c.y, r + 5.0f, r + 5.0f, 0.0f, 0.0f, pitchAngle (shown), true);
        g.setColour ((shown > 0.0f ? ff::teal : ff::coral).withAlpha (0.9f));
        g.strokePath (arc, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // the hand shows what you hear; a ghost hand marks the knob when Jump, Dive or expression moved it
    const float knob = (float) slider.getValue();
    if (std::abs (knob - shown) > 0.2f)
    {
        Path ghost;
        ghost.addRoundedRectangle (-2.0f, -r * 0.66f, 4.0f, r * 0.5f, 1.5f);
        g.setColour (ff::faint);
        g.fillPath (ghost, AffineTransform::rotation (pitchAngle (knob)).translated (c));
    }
    Path hand;
    hand.addRoundedRectangle (-5.0f, -r * 0.70f, 10.0f, r * 0.70f + r * 0.14f, 3.0f);
    g.setColour (ff::ink);
    g.fillPath (hand, AffineTransform::rotation (pitchAngle (shown)).translated (c));
    g.fillEllipse (c.x - 11.0f, c.y - 11.0f, 22.0f, 22.0f);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - 3.5f, c.y - 3.5f, 7.0f, 7.0f);
}

// =====================================================================================
// Magic
// =====================================================================================
MagicDial::MagicDial (OilSlickProcessor& p) : proc (p)
{
    setupRotary (slider);
    slider.getProperties().set ("hidden", true);
    slider.onDragStart = [&p] { p.focusHere(); };
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.apvts, ParamIDs::magic, slider);
    slider.setDoubleClickReturnValue (true, 0.4);
}

void MagicDial::resized() { slider.setBounds (getLocalBounds()); }

void MagicDial::tick()
{
    const float e = proc.getEngine().uiMagic.load();
    if (std::abs (e - eff) > 0.003f) { eff = e; repaint(); }
}

void MagicDial::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const Point<float> c = b.getCentre();
    const float r = jmin (b.getWidth(), b.getHeight()) * 0.5f - 1.0f;
    const float start = -degreesToRadians (135.0f), span = degreesToRadians (270.0f);
    g.setColour (ff::ring);
    g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - r * 0.9f, c.y - r * 0.9f, r * 1.8f, r * 1.8f);
    g.setColour (ff::ink);
    for (int i = 0; i <= 10; ++i)
    {
        const float a = start + span * (float) i / 10.0f;
        g.drawLine ({ { c.x + std::sin (a) * r * 0.80f, c.y - std::cos (a) * r * 0.80f },
                      { c.x + std::sin (a) * r * 0.92f, c.y - std::cos (a) * r * 0.92f } }, 2.5f);
    }
    // the arc shows Magic as you hear it, the runaway pushing past the end of the knob
    if (eff > 0.01f)
    {
        Path arc;
        arc.addCentredArc (c.x, c.y, r * 0.96f, r * 0.96f, 0.0f, start, start + span * jmin (os::kMagicMax, eff), true);
        g.setColour ((eff >= 0.85f ? ff::coral : ff::red).withAlpha (0.85f));
        g.strokePath (arc, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    const float pos = (float) slider.getValue();
    Path hand;
    hand.addRoundedRectangle (-4.0f, -r * 0.70f, 8.0f, r * 0.70f + r * 0.14f, 2.5f);
    g.setColour (ff::red);
    g.fillPath (hand, AffineTransform::rotation (start + pos * span).translated (c));
    g.fillEllipse (c.x - 9.0f, c.y - 9.0f, 18.0f, 18.0f);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - 3.0f, c.y - 3.0f, 6.0f, 6.0f);
}

String EdgeMeter::zoneName (float m)
{
    if (m < 0.01f) return "OFF";
    if (m < 0.3f) return "REPEATS";
    if (m < 0.5f) return "CHORUS";
    if (m < 0.85f) return "PIXIE TRAILS";
    if (m < 1.0f) return "RUNAWAY";
    return "BURN";
}

void EdgeMeter::tick()
{
    const float e = isOn (proc, ParamIDs::bypass) ? 0.0f : proc.getEngine().uiMagic.load();
    if (std::abs (e - eff) > 0.003f) { eff = e; repaint(); }
}

void EdgeMeter::paint (Graphics& g)
{
    const float w = (float) getWidth();
    const bool hot = eff >= 0.85f;
    ff::label (g, "LOOP GAIN", { 0, 0, 120, 16 }, Justification::centredLeft, ff::dim, 11.0f);
    ff::label (g, zoneName (eff), { 0, 0, getWidth(), 16 }, Justification::centredRight, hot ? ff::coral : ff::ink, 11.0f);
    const Rectangle<float> bar (0.0f, 22.0f, w, 12.0f);
    g.setColour (ff::card);
    g.fillRoundedRectangle (bar, 6.0f);
    g.setColour (hot ? ff::coral : ff::teal);
    g.fillRoundedRectangle (bar.withWidth (jmax (12.0f, w * jmin (1.0f, eff / os::kMagicMax))), 6.0f);
    g.setColour (ff::ink.withAlpha (0.5f));
    g.fillRect (w * 0.85f / os::kMagicMax, 22.0f, 2.0f, 12.0f);   // where it starts feeding itself
    static const char* const zones[5] = { "REPEAT", "CHORUS", "PIXIE", "RUNAWAY", "BURN" };
    static const float edges[6] = { 0.0f, 0.3f, 0.5f, 0.85f, 1.0f, 9.0f };
    for (int i = 0; i < 5; ++i)
    {
        const bool lit = eff > 0.01f && eff >= edges[i] && eff < edges[i + 1];
        ff::label (g, zones[i], { (int) (w * (float) i / 5.0f), 38, (int) (w / 5.0f), 14 }, Justification::centred,
                   lit ? (i >= 3 ? ff::coral : ff::ink) : ff::dim, 10.0f);
    }
}

void HoldButton::set (const String& t, bool l, Colour c)
{
    if (t == text && l == lit && c == litColour) return;
    text = t; lit = l; litColour = c;
    repaint();
}

void HoldButton::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (lit ? litColour : (pressed ? ff::cardHi : ff::card));
    g.fillRoundedRectangle (b, 10.0f);
    ff::label (g, text, getLocalBounds(), Justification::centred, lit ? ff::bg : ff::ink, 14.0f);
}

// =====================================================================================
// The footswitch strip
// =====================================================================================
void FootButton::tick()
{
    const bool on = ! isOn (proc, ParamIDs::bypass);
    const auto& eng = proc.getEngine();
    String b, s;
    Colour l = ff::pin;
    bool lt = false;
    switch (sw)
    {
        case 0:
            b = on ? "ON" : "BYPASSED"; s = "TAP BYPASS" + dot + "HOLD MOMENTARY";
            lt = on; if (on) l = ff::red;
            break;
        case 1:
        {
            const bool run = eng.uiRunaway.load() > 0.05f, magic = isOn (proc, ParamIDs::magicOn);
            b = run ? "RUNAWAY" : (magic ? "MAGIC" : "MAGIC OFF"); s = "TAP MAGIC" + dot + "HOLD TO RUN AWAY";
            lt = run || magic;
            if (run) l = std::fmod (nowSeconds() * 3.0, 1.0) < 0.5 ? ff::coral : ff::amber;
            else if (magic) l = ff::coral;
            break;
        }
        case 2:
        {
            const bool dive = eng.uiDive.load() > 0.05f, jump = isOn (proc, ParamIDs::jump);
            b = dive ? "DIVE" : (jump ? "JUMPED" : "JUMP"); s = "TAP JUMP A 3RD" + dot + "HOLD DIVE";
            lt = dive || jump; if (lt) l = ff::teal;
            break;
        }
        default:
        {
            const bool fr = isOn (proc, ParamIDs::freeze);
            b = fr ? "FROZEN" : "FREEZE"; s = "TAP FREEZE" + dot + "HOLD MOMENTARY";
            lt = fr; if (fr) l = ff::coral;
            break;
        }
    }
    const bool d = (proc.uiFootDown.load() >> sw) & 1;
    if (b != big || s != small || l != led || lt != lit || d != down)
    {
        big = b; small = s; led = l; lit = lt; down = d;
        repaint();
    }
}

void FootButton::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (down ? ff::ring : ff::housing);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (lit ? led : ff::line);
    g.drawRoundedRectangle (r.reduced (1.0f), 12.0f, 2.0f);

    const Point<float> p (r.getX() + 24.0f, r.getCentreY());
    if (lit)
    {
        g.setColour (led.withAlpha (0.22f));
        g.fillEllipse (p.x - 12.0f, p.y - 12.0f, 24.0f, 24.0f);
    }
    g.setColour (lit ? led : ff::pin);
    g.fillEllipse (p.x - 7.0f, p.y - 7.0f, 14.0f, 14.0f);

    auto text = r.withTrimmedLeft (46.0f).reduced (0.0f, 8.0f).toNearestInt();
    ff::label (g, String::charToString ((juce_wchar) ('A' + sw)), text.removeFromTop (14), Justification::centredLeft, ff::dim, 11.0f);
    ff::label (g, big, text.removeFromTop (24), Justification::centredLeft, lit ? ff::ink : ff::dim, 19.0f);
    ff::label (g, small, text, Justification::centredLeft, ff::dim, 10.0f);
}

ExpressionBar::ExpressionBar (OilSlickProcessor& p)
    : proc (p),
      pitchChip ("PITCH", [this] { setParam (target, (float) names::ExpPitch); }, 11.0f),
      magicChip ("MAGIC", [this] { setParam (target, (float) names::ExpMagic); }, 11.0f)
{
    target = p.apvts.getParameter (ParamIDs::expTarget);
    pitchChip.setColour (ff::teal);
    magicChip.setColour (ff::coral);
    addAndMakeVisible (pitchChip);
    addAndMakeVisible (magicChip);
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void ExpressionBar::resized()
{
    pitchChip.setBounds (getWidth() - 136, 0, 66, 24);
    magicChip.setBounds (getWidth() - 66, 0, 66, 24);
}

void ExpressionBar::tick()
{
    const int t = roundToInt (raw (proc, ParamIDs::expTarget));
    pitchChip.setLit (t == names::ExpPitch);
    magicChip.setLit (t == names::ExpMagic);
    const float v = proc.expressionLevel();
    if (t != lastTarget || std::abs (v - level) > 0.002f) { lastTarget = t; level = v; repaint(); }
    else if (proc.isCalibrating() || proc.isLearning()) repaint();
}

void ExpressionBar::paint (Graphics& g)
{
    const auto& t = names::expTargets();
    const int which = jlimit (0, (int) t.size() - 1, roundToInt (raw (proc, ParamIDs::expTarget)));
    String head = "EXPRESSION";
    if (which > names::ExpMagic) head += dot + String (t[(size_t) which].label).toUpperCase();
    head += "  v";
    if (proc.isCalibrating()) head = "ROCK HEEL TO TOE";
    else if (proc.learningWhat() == OilSlickProcessor::LearnExpression) head = "MOVE THE PEDAL";
    ff::label (g, head, { 0, 0, getWidth() - 140, 24 }, Justification::centredLeft, proc.isCalibrating() ? ff::ink : ff::dim, 11.0f);

    const float x0 = 26.0f, w = (float) getWidth() - 52.0f, y = 42.0f;
    g.setColour (ff::card);
    g.fillRoundedRectangle (x0, y - 4.0f, w, 8.0f, 4.0f);

    // musical marks for the two shaped targets
    auto tick = [&] (float pos, float h, const String& text)
    {
        g.setColour (ff::dim);
        g.fillRect (x0 + w * pos - 1.0f, y - h * 0.5f, 2.0f, h);
        if (text.isNotEmpty())
            ff::label (g, text, Rectangle<float> (60.0f, 14.0f).withCentre ({ x0 + w * pos, y + 18.0f }).toNearestInt(), Justification::centred, ff::dim, 10.0f);
    };
    const String down = String::fromUTF8 ("\xe2\x86\x93"), up = String::fromUTF8 ("\xe2\x86\x91");
    if (which == names::ExpPitch)
    {
        g.setColour (ff::line);
        g.fillRect (x0 + w * 0.46f, y - 4.0f, w * 0.08f, 8.0f);   // the unison rest zone
        for (int st = -5; st <= 4; ++st)
        {
            String text;
            if (st == -5) text = "4TH " + down; else if (st == -3) text = "MIN 3RD " + down; else if (st == 0) text = "UNI";
            else if (st == 2) text = "2ND " + up; else if (st == 4) text = "3RD " + up;
            tick (pitchToExpression ((float) st), st == 0 ? 22.0f : 14.0f, text);
        }
    }
    else if (which == names::ExpMagic)
    {
        auto inverse = [] (float m) { float lo = 0, hi = 1; for (int i = 0; i < 24; ++i) { const float mid = 0.5f * (lo + hi); (expressionToMagic (mid) < m ? lo : hi) = mid; } return lo; };
        tick (0.0f, 14.0f, "REPEATS");
        tick (inverse (0.5f), 14.0f, "PIXIE");
        tick (inverse (0.85f), 22.0f, "RUNAWAY");
    }

    const bool fresh = nowSeconds() - proc.lastExpressionMove() < 1.5;
    const Colour knob = which == names::ExpMagic ? ff::coral : (which == names::ExpPitch ? ff::teal : ff::ink);
    g.setColour (fresh ? knob : ff::dim);
    g.fillEllipse (x0 + w * level - 10.0f, y - 10.0f, 20.0f, 20.0f);
    g.setColour (ff::face);
    g.drawEllipse (x0 + w * level - 10.0f, y - 10.0f, 20.0f, 20.0f, 3.0f);

    ff::label (g, "HEEL", { 0, getHeight() - 14, 60, 14 }, Justification::centredLeft, ff::dim, 10.0f);
    ff::label (g, "TOE", { getWidth() - 60, getHeight() - 14, 60, 14 }, Justification::centredRight, ff::dim, 10.0f);
    const String note = which == names::ExpPitch ? "SNAPS GENTLY TO INTERVALS" + dot + "UNISON REST ZONE"
                      : which == names::ExpMagic ? "MOST OF THE TRAVEL NEAR THE EDGE" : "CLICK FOR CALIBRATE AND LEARN";
    ff::label (g, note, { 60, getHeight() - 14, getWidth() - 120, 14 }, Justification::centred, ff::dim, 10.0f);
}

void ExpressionBar::mouseDown (const MouseEvent&)
{
    proc.focusHere();
    PopupMenu targets;
    const auto& t = names::expTargets();
    const int cur = roundToInt (raw (proc, ParamIDs::expTarget));
    for (int i = 0; i < (int) t.size(); ++i) targets.addItem (100 + i, t[(size_t) i].label, true, i == cur);
    PopupMenu curves;
    const char* const curveNames[3] = { "Linear", "Slow start", "Fast start" };
    for (int i = 0; i < 3; ++i) curves.addItem (200 + i, curveNames[i], true, proc.pedals().curve == i);

    PopupMenu m;
    m.addSubMenu ("Expression moves", targets);
    m.addItem (1, "Calibrate heel and toe");
    m.addItem (2, "Learn expression pedal");
    m.addSubMenu ("Curve", curves);
    m.addItem (3, "Min, max and smoothing on the Pedal page");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [this] (int r)
    {
        if (r == 1) proc.calibrateExpression();
        else if (r == 2) proc.learnExpression();
        else if (r == 3) { if (showPedalPage) showPedalPage(); }
        else if (r >= 100 && r < 200) setParam (target, (float) (r - 100));
        else if (r >= 200 && r < 300) proc.setCurve (r - 200);
        repaint();
    });
}

void FootInputLabel::paint (Graphics& g)
{
    g.setColour (proc.pedalsDirect() ? ff::green : ff::pin);
    g.fillEllipse (0.0f, (float) getHeight() * 0.5f - 5.0f, 10.0f, 10.0f);
    ff::label (g, proc.pedalsDirect() ? "FOOTSWITCHES READY  v" : "FOOTSWITCHES  v", getLocalBounds().withTrimmedLeft (16), Justification::centredLeft, ff::dim, 12.0f);
}

void FootInputLabel::mouseDown (const MouseEvent&)
{
    PopupMenu m;
    m.addSectionHeader ("Footswitch input");
    m.addItem (-1, proc.footInputStatus(), false, false);
    const auto chosen = proc.footDevice();
    m.addItem (10, "Find my footswitch automatically", true, chosen.isEmpty());
    const auto devices = DirectMidiInput::deviceNames();
    for (int i = 0; i < devices.size() && i < 40; ++i)
        m.addItem (100 + i, "Use " + devices[i], true, chosen == devices[i]);
    m.addItem (11, "Only from a MIDI track in Ableton", true, chosen == "none");

    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [this, devices] (int r)
    {
        if (r == 10) proc.setFootDevice ({});
        else if (r == 11) proc.setFootDevice ("none");
        else if (r >= 100 && r - 100 < devices.size()) proc.setFootDevice (devices[r - 100]);
        repaint();
    });
}

// =====================================================================================
// Pedal page
// =====================================================================================
FootswitchCard::FootswitchCard (OilSlickProcessor& p, int s)
    : proc (p), sw (s),
      learn ("LEARN", [this] { proc.learnFootswitch (sw); }),
      reset ("DEFAULTS", [this] { proc.resetFootswitches(); })
{
    letter.setTextHeight (54.0f);
    letter.setValue (String::charToString ((juce_wchar) ('A' + s)));
    addAndMakeVisible (letter);
    addAndMakeVisible (learn);
    addAndMakeVisible (reset);
}

void FootswitchCard::resized()
{
    letter.setBounds (16, 40, getWidth() - 32, 90);
    learn.setBounds (16, getHeight() - 50, (getWidth() - 40) / 2, 32);
    reset.setBounds (24 + (getWidth() - 40) / 2, getHeight() - 50, (getWidth() - 40) / 2, 32);
}

void FootswitchCard::paint (Graphics& g)
{
    g.setColour (ff::face);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);
    ff::label (g, "FOOTSWITCH", { 16, 12, 200, 18 });

    static const char* const taps[4] = { "BYPASS", "MAGIC ON/OFF", "JUMP A 3RD", "FREEZE" };
    static const char* const holds[4] = { "MOMENTARY", "RUN AWAY", "DIVE", "WHILE HELD" };
    static const char* const notes[4] = { "ACTS ON THE PRESS", "CLIMBS INTO OSCILLATION", "GLIDES TO A 4TH DOWN", "PLAY OVER THE HELD TRAIL" };
    static const char* const combos[4] = { "A+B  TAME", "A+B  TAME", "C+D  NEXT COPY", "C+D HELD 1 S  KEEPER" };
    struct Row { const char* k; String v; };
    const Row rows[3] = { { "TAP", taps[sw] }, { "HOLD", holds[sw] }, { "LISTENS FOR", sends } };
    const int w = getWidth() - 32;
    int y = 146;
    for (const auto& r : rows)
    {
        g.setColour (ff::ring);
        g.fillRect (16, y, w, 2);
        ff::label (g, r.k, { 16, y + 4, 100, 30 }, Justification::centredLeft, ff::dim, 12.0f);
        ff::label (g, r.v, { 16, y + 4, w, 30 }, Justification::centredRight, ff::ink, 13.0f);
        y += 38;
    }
    g.setColour (ff::ring);
    g.fillRect (16, y, w, 2);
    ff::label (g, notes[sw], { 16, y + 8, w, 16 }, Justification::centredLeft, ff::dim, 11.0f);
    ff::label (g, combos[sw], { 16, y + 30, w, 16 }, Justification::centredLeft, ff::dim, 11.0f);
    if (sw >= 2) ff::label (g, sw == 2 ? "C+D HELD 1 S  KEEPER" : "C+D  NEXT COPY", { 16, y + 50, w, 16 }, Justification::centredLeft, ff::dim, 11.0f);
}

void FootswitchCard::tick (double dt)
{
    const auto& s = proc.pedals();
    const String t = "CH " + String (s.fsChannel[(size_t) sw]) + dot + "CC " + String (s.fsCC[(size_t) sw]);
    if (t != sends) { sends = t; repaint(); }
    learn.setLit (proc.learningWhat() == sw);
    learn.setText (proc.learningWhat() == sw ? "PRESS IT" : "LEARN");
    const bool down = (proc.uiFootDown.load() >> sw) & 1;
    letter.setAlpha (down ? 0.6f : 1.0f);
    letter.advance (dt);
}

// =====================================================================================
// Pebble: floats up with the pitch, sinks with it, wobbles as the Magic loop feeds itself
// and loses it completely in a runaway. Sleeps when bypassed, holds its breath for Freeze.
// =====================================================================================
void Pebble::update (double t, double dt)
{
    now = t;
    auto& eng = proc.getEngine();
    asleep = isOn (proc, ParamIDs::bypass);
    pitch = eng.uiPitch.load();
    magic = asleep ? 0.0f : eng.uiMagic.load();
    frozen = ! asleep && eng.uiFrozen.load();
    level = eng.uiOutput.load();
    const float w = asleep ? 0.0f : jmax (eng.uiRunaway.load(), jlimit (0.0f, 1.0f, (magic - 0.85f) / 0.25f));
    wild += (w - wild) * (float) jmin (1.0, dt * 8.0);

    const float leanTarget = t - proc.lastExpressionMove() < 1.5 ? (proc.expressionLevel() - 0.5f) * 0.3f : 0.0f;
    lean += (leanTarget - lean) * (float) jmin (1.0, dt * 6.0);

    if (proc.uiFootPulse.load() != lastFoot) { lastFoot = proc.uiFootPulse.load(); hop = 1.0f; }
    if (proc.uiKeepPulse.load() != lastKeep) { lastKeep = proc.uiKeepPulse.load(); nod = 1.0f; }
    if (proc.uiTamePulse.load() != lastTame) { lastTame = proc.uiTamePulse.load(); calm = 1.0f; }
    hop = jmax (0.0f, hop - (float) dt * 4.0f);
    nod = jmax (0.0f, nod - (float) dt * 1.6f);
    calm = jmax (0.0f, calm - (float) dt * 0.6f);

    ringAngle = std::fmod (ringAngle + (float) dt * (15.0f + 650.0f * wild + std::abs (pitch) * 20.0f), 360.0f);

    if (t > nextBlink) { blinking = true; blinkUntil = t + 0.14; nextBlink = t + 2.5 + Random::getSystemRandom().nextDouble() * 4.0; }
    if (blinking && t > blinkUntil) blinking = false;
    repaint();
}

String Pebble::mood() const
{
    const double ago = nowSeconds() - proc.uiMessageAt;
    if (ago < 2.5 && proc.uiMessage.isNotEmpty()) return proc.uiMessage;
    if (asleep) return "DOZING";
    if (calm > 0.3f) return "CALM";
    if (wild > 0.6f) return "LOSING IT";
    if (wild > 0.15f) return "WOBBLING";
    if (frozen) return "FROZEN";
    if (proc.getEngine().uiDive.load() > 0.3f) return "DIVING";
    if (pitch > 0.5f) return "FLOATING UP";
    if (pitch < -0.5f) return "SINKING";
    return "SLICK";
}

String Pebble::hint() const
{
    if (asleep) return "TAP A TO WAKE PEBBLE";
    if (calm > 0.3f) return "TAME CLEARED THE FEEDBACK";
    if (wild > 0.6f) return "LET GO OF B TO REEL IT IN" + dot + "A+B TAMES";
    if (wild > 0.15f) return "THE LOOP IS FEEDING ITSELF";
    if (frozen) return "PEBBLE HOLDS ITS BREATH" + dot + "PLAY OVER IT";
    if (proc.getEngine().uiDive.load() > 0.3f) return "LET GO OF C TO SPRING BACK";
    const String interval = names::intervalName (roundToInt (pitch)).toUpperCase();
    if (pitch > 0.5f) return interval + dot + (magic > 0.1f ? "TRAILS CLIMB" : "HARMONY ABOVE");
    if (pitch < -0.5f) return interval + dot + (magic > 0.1f ? "TRAILS FALL" : "HARMONY BELOW");
    return magic > 0.3f ? "PIXIE TRAILS ROUND PEBBLE" : "UNISON" + dot + "A WOBBLY CHORUS";
}

void Pebble::paint (Graphics& g)
{
    auto a = getLocalBounds().toFloat();
    const float s = a.getWidth() / 240.0f;
    const auto tf = AffineTransform::translation (-100.0f, -106.0f).scaled (s).translated (a.getCentreX(), a.getCentreY());
    const float t = (float) now;

    // the oil-slick ring: its colours shift with the pitch and it spins faster as the loop runs away
    const float activity = jlimit (0.0f, 1.0f, 0.25f + 0.12f * std::abs (pitch) + 0.6f * jmin (1.0f, magic));
    if (! asleep)
    {
        if (frozen)
        {
            Path rp; rp.addEllipse (100 - 86, 106 - 86, 172, 172);
            g.setColour (ff::coral.withAlpha (0.75f));
            g.strokePath (rp, PathStrokeType (5.0f), tf);
        }
        else
        {
            const float pulse = 92.0f * (1.0f + 0.06f * wild * std::sin (t * 9.0f));
            const float alpha = 0.35f + 0.6f * activity;
            for (int k = 0; k < 72; ++k)
            {
                const float a0 = degreesToRadians ((float) k * 5.0f + ringAngle);
                Path seg;
                seg.addCentredArc (100.0f, 106.0f, pulse, pulse, 0.0f, a0, a0 + degreesToRadians (5.6f), true);
                g.setColour (ff::slick ((float) k / 72.0f + pitch * 0.07f).withAlpha (alpha));
                g.strokePath (seg, PathStrokeType (9.0f), tf);
            }
        }
    }

    // pixie trails: dots spinning off Pebble, climbing or falling with the pitch
    const float pixie = asleep ? 0.0f : jlimit (0.0f, 1.0f, (magic - 0.3f) / 0.5f);
    if (pixie > 0.02f)
        for (int i = 0; i < 16; ++i)
        {
            const float ph = std::fmod (t * (0.35f + 0.5f * wild) + (float) i / 16.0f, 1.0f);
            const float ang = degreesToRadians ((float) i * 51.0f + t * (40.0f + 160.0f * wild));
            const float rad = 60.0f + 54.0f * ph;
            const Point<float> p (100.0f + std::cos (ang) * rad, 106.0f + std::sin (ang) * rad * 0.85f - pitch * 8.0f * ph);
            const float sz = 3.0f + 4.0f * wild * (1.0f - ph) + 2.0f * pixie;
            g.setColour (ff::slick ((float) i / 6.0f).withAlpha ((1.0f - ph) * pixie));
            const auto q = p.transformedBy (tf);
            g.fillEllipse (q.x - sz * s * 0.5f, q.y - sz * s * 0.5f, sz * s, sz * s);
        }

    // the body
    const float bob = asleep ? 0.0f : 1.5f * std::sin (t * 2.4f);
    const float jx = wild * 3.2f * (std::sin (t * 53.0f) + std::sin (t * 31.7f));
    const float jy = wild * 3.2f * (std::cos (t * 47.0f) + std::sin (t * 23.3f));
    const float wob = std::sin (t * 14.0f);
    const float breathe = asleep ? 1.0f + 0.025f * std::sin (t * 1.6f) : 1.0f;
    const float sx = (1.0f - 0.022f * pitch + 0.09f * wild * wob + 0.05f * nod) * breathe;
    const float sy = (1.0f + 0.04f * pitch - 0.09f * wild * wob - 0.08f * nod * std::sin (nod * 9.0f)) / breathe;
    const float dy = bob - pitch * 2.2f - hop * 10.0f;
    const auto body = AffineTransform::translation (-100.0f, -106.0f).scaled (sx, sy).rotated (lean)
                          .translated (100.0f + jx, 106.0f + jy + dy).followedBy (tf);
    auto ell = [&] (float cx, float cy, float rx, float ry, Colour col)
    {
        Path p; p.addEllipse (cx - rx, cy - ry, rx * 2, ry * 2);
        g.setColour (col); g.fillPath (p, body);
    };
    auto stroke = [&] (Path& p, float w)
    {
        g.setColour (ff::faceInk);
        g.strokePath (p, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded), body);
    };

    ell (107, 109, 58, 48, ff::blobShade);
    ell (100, 100, 58, 48, ff::blob);

    if (asleep || blinking)
    {
        for (float ex : { 80.0f, 120.0f })
        {
            Path lid; lid.startNewSubPath (ex - 12, 94); lid.quadraticTo (ex, 102, ex + 12, 94);
            stroke (lid, 6.0f);
        }
    }
    else
    {
        // eyes look up with the pitch, down as it sinks, and spin in a runaway
        const float lookX = 2.5f * std::sin (t * 0.7f) * (1.0f - wild), lookY = jlimit (-6.0f, 6.0f, -pitch * 1.4f) * (1.0f - wild);
        const float orb = 6.0f * wild, eyeR = 14.0f + 3.0f * wild, pupil = 7.0f - 3.0f * wild;
        for (int k = 0; k < 2; ++k)
        {
            const float ex = k == 0 ? 80.0f : 120.0f, ph = t * 11.0f + (k == 0 ? 0.0f : MathConstants<float>::pi);
            ell (ex, 92, eyeR, eyeR, ff::eye);
            ell (ex + lookX + std::cos (ph) * orb, 94 + lookY + std::sin (ph) * orb, pupil, pupil, ff::faceInk);
        }
    }

    if (asleep)
    {
        Path m; m.startNewSubPath (94, 124); m.lineTo (106, 124);
        stroke (m, 6.0f);
    }
    else if (wild > 0.2f)
        ell (100, 124, 8.0f + 5.0f * wild, 5.0f + 11.0f * wild * (0.8f + 0.2f * std::sin (t * 19.0f)), ff::faceInk);
    else if (frozen)
        ell (100, 123, 6.0f, 6.0f, ff::faceInk);   // holding its breath
    else if (level > 0.25f)
        ell (100, 124, 7.0f + 3.0f * level, 4.0f + 8.0f * jmin (1.0f, level), ff::faceInk);
    else if (pitch < -1.5f)
    {
        Path m; m.startNewSubPath (90, 127); m.quadraticTo (100, 118, 110, 127);
        stroke (m, 6.0f);
    }
    else
    {
        Path m; m.startNewSubPath (90, 120); m.quadraticTo (100, 130 + jlimit (0.0f, 4.0f, pitch), 110, 120);
        stroke (m, 6.0f);
    }

    if (asleep)
    {
        g.setFont (ff::font (16.0f, 0.0f));
        for (int k = 0; k < 3; ++k)
        {
            const float ph = (float) std::fmod (now * 0.5 + k / 3.0, 1.0);
            g.setColour (ff::ink.withAlpha (1.0f - ph));
            g.drawText ("z", Rectangle<float> (a.getRight() - 40.0f + ph * 10.0f, a.getY() + 40.0f - ph * 22.0f, 16.0f, 16.0f),
                        Justification::centred, false);
        }
    }
}

// =====================================================================================
// Editor
// =====================================================================================
OilSlickEditor::OilSlickEditor (OilSlickProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      version (p.apvts, ParamIDs::version, { "V2", "FIRST EDITION" }, 12.0f),
      trails (p.apvts, ParamIDs::trails, "CUT TAILS", "TRAILS", 12.0f),
      mainTab ("MAIN", [this] { setPage (0); }),
      pedalTab ("PEDAL", [this] { setPage (1); }),
      keepButton ("KEEP", [this] { proc.keepNow(); }),
      keepMenu ("v", [this] { keeperMenu(); }),
      footInput (p),
      pitchDial (p),
      primary (p, ParamIDs::primary, "Primary", "Main voice"),
      secondary (p, ParamIDs::secondary, "Secondary", "Noon is off"),
      tracking (p, ParamIDs::tracking, "Tracking", "Lag before the shift"),
      tone (p, ParamIDs::tone, "Tone", "Dark to bright"),
      magicDial (p),
      magicSwitch (p.apvts, ParamIDs::magicOn, "SWITCH OFF", "SWITCH ON", 12.0f, ff::coral),
      edge (p),
      tameButton ("TAME", [this] { proc.tame(); }, 14.0f),
      pebble (p),
      expression (p),
      expMin (p.apvts, ParamIDs::expMin, "Heel sets"),
      expMax (p.apvts, ParamIDs::expMax, "Toe sets"),
      expSmooth (p.apvts, ParamIDs::expSmooth, "Smoothing"),
      calibrate ("CALIBRATE", [this] { proc.calibrateExpression(); }),
      learnExp ("LEARN", [this] { proc.learnExpression(); }),
      curve ("CURVE", [this] { proc.setCurve ((proc.pedals().curve + 1) % 3); }),
      liveChip ("STUDIO", [this] { proc.setKeeperLive (! proc.keeperLive()); }),
      learnKeep ("LEARN SWITCH", [this] { proc.learnKeeper(); }),
      exportKeep ("EXPORT", [this] { proc.exportKeepers(); }),
      folderKeep ("FOLDER", [this] { proc.openKeepersFolder(); })
{
    setLookAndFeel (&lnf);
    tracking.valueText = [this]
    {
        const float ms = os::trackingMs (raw (proc, ParamIDs::tracking), roundToInt (raw (proc, ParamIDs::version)));
        return ms < 12.0f ? String ("TIGHT") : String (roundToInt (ms)) + " MS";
    };

    for (auto* c : std::initializer_list<Component*> { &version, &trails, &mainTab, &pedalTab, &keepButton, &keepMenu, &footInput,
                                                       &pitchDial, &pitchCard, &primary, &secondary, &tracking, &tone,
                                                       &magicDial, &magicSwitch, &edge, &tameButton, &runButton, &pebble, &moodCard,
                                                       &expression, &expMin, &expMax, &expSmooth, &calibrate, &learnExp, &curve,
                                                       &liveChip, &learnKeep, &exportKeep, &folderKeep })
        addAndMakeVisible (c);

    pitchCard.setTextHeight (34.0f);
    moodCard.setTextHeight (24.0f);
    runButton.onDown = [this] { proc.focusHere(); setParam (proc.apvts.getParameter (ParamIDs::runaway), 1.0f); };
    runButton.onUp = [this] { setParam (proc.apvts.getParameter (ParamIDs::runaway), 0.0f); };
    expression.showPedalPage = [this] { setPage (1); };

    for (int i = 0; i < 4; ++i)
    {
        feet[(size_t) i] = std::make_unique<FootButton> (p, i);
        addAndMakeVisible (*feet[(size_t) i]);
        cards[(size_t) i] = std::make_unique<FootswitchCard> (p, i);
        addChildComponent (*cards[(size_t) i]);
    }
    const auto& t = names::expTargets();
    for (int i = 0; i < 6; ++i)
    {
        targetChips[(size_t) i] = std::make_unique<ActionChip> (String (t[(size_t) i].label).toUpperCase(),
            [this, i] { setParam (proc.apvts.getParameter (ParamIDs::expTarget), (float) i); }, 11.0f);
        addChildComponent (*targetChips[(size_t) i]);
    }

    noteEditor.setFont (ff::font (14.0f));
    noteEditor.setColour (TextEditor::backgroundColourId, ff::card);
    noteEditor.setColour (TextEditor::textColourId, ff::ink);
    noteEditor.setColour (TextEditor::outlineColourId, ff::line);
    noteEditor.setTextToShowWhenEmpty ("A NOTE FOR THIS KEEPER, THEN ENTER", ff::dim);
    noteEditor.onReturnKey = [this] { closeNote (true); };
    noteEditor.onEscapeKey = [this] { closeNote (false); };
    addChildComponent (noteEditor);

    setPage (0);
    setSize (1260, 660);
    start = lastTick = nowSeconds();
    startTimerHz (30);
}

OilSlickEditor::~OilSlickEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void OilSlickEditor::setPage (int p)
{
    page = p;
    const bool main = p == 0;
    for (auto* c : std::initializer_list<Component*> { &pitchDial, &pitchCard, &primary, &secondary, &tracking, &tone,
                                                       &magicDial, &magicSwitch, &edge, &tameButton, &runButton, &pebble, &moodCard })
        c->setVisible (main);
    for (auto& c : cards) c->setVisible (! main);
    for (auto& c : targetChips) c->setVisible (! main);
    for (auto* c : std::initializer_list<Component*> { &expMin, &expMax, &expSmooth, &calibrate, &learnExp, &curve,
                                                       &liveChip, &learnKeep, &exportKeep, &folderKeep })
        c->setVisible (! main);
    mainTab.setLit (main);
    pedalTab.setLit (! main);
    repaint();
}

void OilSlickEditor::keeperMenu()
{
    PopupMenu m;
    m.addItem (1, "Add a note to Keeper No. " + String (proc.lastKeeper()), proc.lastKeeper() > 0);
    m.addItem (2, "Export for Claude");
    m.addItem (3, "Open the Keepers folder");
    m.addSeparator();
    m.addItem (5, "Live", true, proc.keeperLive());
    m.addItem (6, "Studio", true, ! proc.keeperLive());
    m.addSeparator();
    m.addItem (4, "Learn a Keeper footswitch");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&keepMenu), [this] (int r)
    {
        if (r == 1)
        {
            noteNumber = proc.lastKeeper();
            noteEditor.clear();
            noteEditor.setVisible (true);
            noteEditor.grabKeyboardFocus();
            noteUntil = nowSeconds() + 20.0;
        }
        else if (r == 2) proc.exportKeepers();
        else if (r == 3) proc.openKeepersFolder();
        else if (r == 4) proc.learnKeeper();
        else if (r == 5 || r == 6) proc.setKeeperLive (r == 5);
    });
}

void OilSlickEditor::closeNote (bool save)
{
    if (save) proc.addKeeperNote (noteNumber, noteEditor.getText());
    if (save && noteEditor.getText().trim().isNotEmpty())
    {
        proc.uiMessage = "NOTED";
        proc.uiMessageAt = nowSeconds();
    }
    noteEditor.setVisible (false);
    noteEditor.clear();
    noteUntil = 0.0;
}

void OilSlickEditor::section (Graphics& g, Rectangle<int> r) const
{
    g.setColour (ff::face);
    g.fillRoundedRectangle (r.toFloat(), 12.0f);
}

void OilSlickEditor::paint (Graphics& g)
{
    g.fillAll (ff::bg);
    g.setColour (ff::ink);
    g.setFont (ff::font (26.0f, 0.06f));
    g.drawText ("OILSLICK", 20, 20, 130, 40, Justification::centredLeft, false);
    ff::label (g, "POLYPHONIC PITCH, SLICK AND UNSTABLE", { 150, 20, 280, 40 });

    section (g, { 20, 532, 1220, 112 });
    ff::label (g, "FOOTSWITCHES" + dot + "TAP OR HOLD, JUST LIKE THE REAL ONES", { 36, 538, 420, 18 }, Justification::centredLeft, ff::dim, 11.0f);
    ff::label (g, "A+B TAME" + dot + "C+D NEXT COPY" + dot + "HOLD C+D 1 S KEEPER", { 440, 538, 396, 18 }, Justification::centredRight, ff::dim, 11.0f);

    if (page == 0)
    {
        section (g, { 20, 76, 400, 440 });
        section (g, { 440, 76, 250, 440 });
        section (g, { 710, 76, 250, 440 });
        section (g, { 980, 76, 260, 440 });
        ff::label (g, "PITCH", { 36, 88, 200, 18 });
        ff::label (g, "VOICES + TIME", { 456, 88, 200, 18 });
        ff::label (g, "MAGIC", { 726, 88, 100, 18 });
        ff::label (g, "PEBBLE", { 996, 88, 200, 18 });

        const float shown = proc.getEngine().uiPitch.load();
        ff::label (g, names::intervalName (roundToInt (shown)).toUpperCase(), { 200, 438, 210, 24 }, Justification::centredLeft, ff::ink, 18.0f);
        String pitchHint = "SEMITONES" + dot + "CHORUS AT NOON";
        if (proc.getEngine().uiDive.load() > 0.05f) pitchHint = "DIVING" + dot + "FOOTSWITCH C";
        else if (isOn (proc, ParamIDs::jump)) pitchHint = "JUMPED" + dot + "TAP C TO RETURN";
        else if (shown > 0.3f) pitchHint = "SEMITONES" + dot + "TRAILS CLIMB";
        else if (shown < -0.3f) pitchHint = "SEMITONES" + dot + "TRAILS FALL";
        ff::label (g, pitchHint, { 200, 464, 210, 18 }, Justification::centredLeft, ff::dim, 11.0f);

        const float m = raw (proc, ParamIDs::magic), eff = proc.getEngine().uiMagic.load();
        String mt = String (roundToInt (m * 100.0f)) + "%";
        if (proc.getEngine().uiRunaway.load() > 0.05f) mt += String::fromUTF8 (" \xe2\x86\x92 ") + String (roundToInt (eff / os::kMagicMax * 100.0f)) + "%";
        ff::label (g, mt, { 726, 300, 218, 28 }, Justification::centred, ff::ink, 22.0f);
        String mh = isOn (proc, ParamIDs::magicOn) ? "SWITCH ON" : "SWITCH OFF" + dot + "THE KNOB WAITS";
        if (isOn (proc, ParamIDs::bypass)) mh = "BYPASSED";
        else if (proc.getEngine().uiRunaway.load() > 0.05f) mh = "RUNNING AWAY";
        ff::label (g, mh, { 726, 328, 218, 16 }, Justification::centred, ff::dim, 11.0f);

        ff::label (g, pebble.hint(), { 990, 460, 240, 18 }, Justification::centred, ff::dim, 11.0f);
    }
    else
    {
        section (g, { 940, 76, 300, 280 });
        section (g, { 940, 366, 300, 150 });
        ff::label (g, "EXPRESSION MOVES", { 956, 88, 260, 18 });
        ff::label (g, "KEEPER", { 956, 378, 120, 18 });
        ff::label (g, "LAST No. " + String (proc.lastKeeper()), { 1080, 378, 144, 18 }, Justification::centredRight, ff::ink, 12.0f);
        ff::label (g, "HOLD C+D FOR A SECOND, OR PRESS KEEP", { 956, 488, 270, 16 }, Justification::centredLeft, ff::dim, 10.5f);
        ff::label (g, "SET ONCE, SHARED BY EVERY FLIP FELLA PLUGIN", { 956, 336, 270, 14 }, Justification::centredLeft, ff::dim, 10.0f);
    }
}

void OilSlickEditor::resized()
{
    // header
    version.setBounds (430, 24, 210, 32);
    trails.setBounds (650, 24, 104, 32);
    mainTab.setBounds (770, 24, 72, 32);
    pedalTab.setBounds (848, 24, 76, 32);
    keepButton.setBounds (940, 24, 72, 32);
    keepMenu.setBounds (1016, 24, 32, 32);
    footInput.setBounds (1062, 20, 178, 40);

    // pitch
    pitchDial.setBounds (36, 106, 368, 318);
    pitchCard.setBounds (36, 432, 152, 68);

    // voices
    primary.setBounds (452, 106, 113, 170);
    secondary.setBounds (565, 106, 113, 170);
    tracking.setBounds (452, 300, 113, 170);
    tone.setBounds (565, 300, 113, 170);

    // magic
    magicSwitch.setBounds (850, 84, 100, 26);
    magicDial.setBounds (750, 120, 170, 170);
    edge.setBounds (726, 354, 218, 54);
    tameButton.setBounds (726, 452, 104, 44);
    runButton.setBounds (840, 452, 104, 44);

    // pebble
    pebble.setBounds (990, 112, 240, 240);
    moodCard.setBounds (996, 380, 228, 64);
    noteEditor.setBounds (996, 482, 228, 28);

    // strip
    for (int i = 0; i < 4; ++i) feet[(size_t) i]->setBounds (36 + i * 202, 560, 196, 72);
    expression.setBounds (856, 540, 368, 96);

    // pedal page
    for (int i = 0; i < 4; ++i) cards[(size_t) i]->setBounds (20 + i * 230, 76, 220, 440);
    for (int i = 0; i < 6; ++i) targetChips[(size_t) i]->setBounds (956 + (i % 3) * 90, 112 + (i / 3) * 34, 84, 28);
    expMin.setBounds (956, 184, 268, 32);
    expMax.setBounds (956, 220, 268, 32);
    expSmooth.setBounds (956, 256, 268, 32);
    calibrate.setBounds (956, 296, 86, 30);
    learnExp.setBounds (1047, 296, 86, 30);
    curve.setBounds (1138, 296, 86, 30);
    liveChip.setBounds (956, 404, 130, 32);
    learnKeep.setBounds (1094, 404, 130, 32);
    exportKeep.setBounds (956, 444, 130, 32);
    folderKeep.setBounds (1094, 444, 130, 32);
}

void OilSlickEditor::timerCallback()
{
    const double now = nowSeconds();
    const double dt = jlimit (0.0, 0.2, now - lastTick);
    lastTick = now;

    for (auto& f : feet) f->tick();
    expression.tick();
    footInput.repaint();

    // Keeper: KEPT on the button for a moment, and the learn state
    const bool kept = proc.uiMessage == "KEPT" && now - proc.uiMessageAt < 1.5;
    keepButton.setLit (kept);
    keepButton.setText (kept ? "KEPT" : (proc.learningWhat() == OilSlickProcessor::LearnKeeper ? "PRESS IT" : "KEEP"));
    if (noteUntil > 0.0 && now > noteUntil) closeNote (true);

    if (page == 0)
    {
        pitchDial.tick();
        magicDial.tick();
        edge.tick();
        pebble.update (now - start, dt);
        pitchCard.setValue (signedSemis (proc.getEngine().uiPitch.load()));
        pitchCard.advance (dt);
        moodCard.setValue (pebble.mood());
        moodCard.advance (dt);
        const bool run = proc.getEngine().uiRunaway.load() > 0.05f;
        runButton.set (run ? "RUNNING AWAY" : "HOLD: RUN AWAY", run, ff::coral);
        repaint (190, 430, 230, 60);
        repaint (720, 296, 230, 52);
        repaint (985, 455, 250, 26);
    }
    else
    {
        for (auto& c : cards) c->tick (dt);
        const int t = roundToInt (raw (proc, ParamIDs::expTarget));
        for (int i = 0; i < 6; ++i) targetChips[(size_t) i]->setLit (i == t);
        const char* const curveNames[3] = { "LINEAR", "SLOW START", "FAST START" };
        curve.setText (curveNames[jlimit (0, 2, proc.pedals().curve)]);
        calibrate.setLit (proc.isCalibrating());
        calibrate.setText (proc.isCalibrating() ? "ROCK IT" : "CALIBRATE");
        learnExp.setLit (proc.learningWhat() == OilSlickProcessor::LearnExpression);
        liveChip.setText (proc.keeperLive() ? "LIVE" : "STUDIO");
        learnKeep.setLit (proc.learningWhat() == OilSlickProcessor::LearnKeeper);
        repaint (940, 370, 300, 30);
    }
}
} // namespace ui
