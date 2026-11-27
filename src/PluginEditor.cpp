#include "PluginEditor.h"
#include "Presets.h"
#include "ui/Render.h"
#include "ui/Logo.h"

namespace
{
    using juce::Colour;

    // ---- Design-size layout (points).
    constexpr int headerHeight = 76;
    constexpr int slotWidth = 178;
    constexpr float pedalWidth = 150.0f, pedalHeight = 312.0f;
    constexpr float knobBody = 40.0f;
    constexpr float jackY = 150.0f;              // jack height on the pedal sides
    constexpr float rotaryStart = juce::MathConstants<float>::pi * 1.25f;
    constexpr float rotaryEnd = juce::MathConstants<float>::pi * 2.75f;

    namespace colours
    {
        const Colour cream  { 0xffefe6d4 };
        const Colour gold   { 0xffd9b36c };
        const Colour amber  { 0xffffa53a };
        const Colour ledRed { 0xffff4a26 };
    }

    juce::Font font (const char* family, const char* style, float height, float tracking = 0.0f)
    {
        return juce::Font (juce::FontOptions (family, style, height).withKerningFactor (tracking));
    }

    juce::Font sans (const char* style, float height, float tracking = 0.0f)      { return font ("Avenir Next", style, height, tracking); }
    juce::Font condensed (const char* style, float height, float tracking = 0.0f) { return font ("Avenir Next Condensed", style, height, tracking); }

    struct PedalStyle
    {
        Colour paint;
        orion::render::Finish finish;
        Colour ink;
        int seed;
    };

    PedalStyle styleFor (const juce::String& id)
    {
        using F = orion::render::Finish;
        if (id == "gate")   return { Colour (0xff4a5058), F::hammertone,    colours::cream, 3 };
        if (id == "od")     return { Colour (0xff35593c), F::metallicFlake, colours::cream, 5 };
        if (id == "amp")    return { Colour (0xff4a1716), F::gloss,         colours::gold,  7 };
        if (id == "pre")    return { Colour (0xff2c5552), F::metallicFlake, colours::cream, 9 };
        if (id == "mod")    return { Colour (0xff2c3858), F::metallicFlake, colours::cream, 11 };
        if (id == "echo")   return { Colour (0xff7c3d22), F::metallicFlake, colours::cream, 13 };
        if (id == "spring") return { Colour (0xffd0c4a8), F::gloss,         Colour (0xff2b231c), 15 };
        return { Colour (0xff505050), F::gloss, colours::cream, 1 };
    }

    std::pair<juce::String, juce::String> switchLabels (const juce::String& paramId, const juce::String& label)
    {
        if (paramId == "amp_cab")     return { "CAB", "DIRECT" };
        if (paramId == "mod_flanger") return { "FLANGER", "CHORUS" };
        return { label.toUpperCase(), {} };
    }

    juce::Path roundedRect (juce::Rectangle<float> r, float radius)
    {
        juce::Path p;
        p.addRoundedRectangle (r, radius);
        return p;
    }

    // Chrome-ish vertical gradient used for small vector hardware (plugs, screws, buttons).
    juce::ColourGradient chromeGradient (juce::Rectangle<float> r)
    {
        juce::ColourGradient g (Colour (0xffeef1f4), r.getX(), r.getY(), Colour (0xff3e4247), r.getX(), r.getBottom(), false);
        g.addColour (0.45, Colour (0xff9ea5ad));
        g.addColour (0.55, Colour (0xff6a7078));
        return g;
    }

    void drawScrew (juce::Graphics& g, juce::Point<float> c, float r, float slotAngle)
    {
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c).translated (0.6f, 1.0f));
        const auto disc = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c);
        g.setGradientFill (chromeGradient (disc));
        g.fillEllipse (disc);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        for (float a : { slotAngle, slotAngle + juce::MathConstants<float>::halfPi })
            g.drawLine (juce::Line<float> (c.getPointOnCircumference (r * 0.7f, a), c.getPointOnCircumference (r * 0.7f, a + juce::MathConstants<float>::pi)), 1.1f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (disc, 0.6f);
    }

    // Silkscreened text: ink with a hairline shadow so it sits on the metal.
    void drawPrinted (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Justification j, Colour ink)
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawText (text, area.translated (0.0f, 1.0f), j);
        g.setColour (ink);
        g.drawText (text, area, j);
    }

}

namespace orion
{

// ---------------------------------------------------------------------------
void CachedArt::draw (juce::Graphics& g, juce::Rectangle<float> area, const Renderer& render)
{
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int w = juce::jmax (1, juce::roundToInt (area.getWidth() * scale));
    const int h = juce::jmax (1, juce::roundToInt (area.getHeight() * scale));

    if (image.isNull() || image.getWidth() != w || image.getHeight() != h)
    {
        if (image.isValid() && ! renderNow)
        {
            // Resizing: stretch what we have and re-render once things settle.
            g.drawImage (image, area);
            if (! pending)
            {
                pending = true;
                juce::Component::SafePointer<juce::Component> safe (&owner);
                juce::Timer::callAfterDelay (220, [safe, this]
                {
                    if (safe != nullptr)
                    {
                        pending = false;
                        renderNow = true;
                        safe->repaint();
                    }
                });
            }
            return;
        }

        image = juce::Image (juce::Image::ARGB, w, h, true);
        juce::Graphics ig (image);
        const float sx = (float) w / area.getWidth(), sy = (float) h / area.getHeight();
        ig.addTransform (juce::AffineTransform::scale (sx, sy));
        render (ig, { area.getWidth(), area.getHeight() }, sx);
        renderNow = false;
    }

    g.drawImage (image, area);
}

// ---------------------------------------------------------------------------
OrionLookAndFeel::OrionLookAndFeel()
{
    setColour (juce::BubbleComponent::backgroundColourId, Colour (0xf0121110));
    setColour (juce::BubbleComponent::outlineColourId, colours::amber.withAlpha (0.35f));
    setColour (juce::TooltipWindow::textColourId, colours::cream);
    setColour (juce::PopupMenu::backgroundColourId, Colour (0xff121010));
    setColour (juce::PopupMenu::textColourId, colours::cream);
    setColour (juce::PopupMenu::headerTextColourId, colours::amber.withAlpha (0.8f));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colour (0xff3b2a17));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);

    // Preset name / confirmation dialogs.
    setColour (juce::AlertWindow::backgroundColourId, Colour (0xff121010));
    setColour (juce::AlertWindow::textColourId, colours::cream);
    setColour (juce::AlertWindow::outlineColourId, colours::amber.withAlpha (0.35f));
    setColour (juce::TextEditor::backgroundColourId, Colour (0xff070605));
    setColour (juce::TextEditor::textColourId, colours::amber);
    setColour (juce::TextEditor::highlightColourId, colours::amber.withAlpha (0.3f));
    setColour (juce::TextEditor::outlineColourId, colours::amber.withAlpha (0.25f));
    setColour (juce::TextEditor::focusedOutlineColourId, colours::amber.withAlpha (0.6f));
    setColour (juce::CaretComponent::caretColourId, colours::amber);
    setColour (juce::TextButton::buttonColourId, Colour (0xff2a2420));
    setColour (juce::TextButton::textColourOffId, colours::cream);
}

juce::Font OrionLookAndFeel::getSliderPopupFont (juce::Slider&) { return sans ("Demi Bold", 13.0f); }
juce::Font OrionLookAndFeel::getPopupMenuFont()                 { return sans ("Medium", 15.0f); }

void OrionLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
    g.setColour (colours::amber.withAlpha (0.18f));
    g.drawRect (0, 0, width, height, 1);
}

// ---------------------------------------------------------------------------
RealKnob::RealKnob (float bodyDiameter) : diameter (bodyDiameter)
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setRotaryParameters (rotaryStart, rotaryEnd, true);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setPopupDisplayEnabled (true, true, nullptr);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void RealKnob::paint (juce::Graphics& g)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const auto bodyArea = juce::Rectangle<float> (diameter, diameter).withCentre (c);
    const float r = diameter * 0.5f;

    // Soft contact shadow, cast down-right by the key light.
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.7f), c.x + 2.0f, c.y + 3.5f,
                                             juce::Colours::transparentBlack, c.x + 2.0f + r * 1.25f, c.y + 3.5f, true));
    g.fillEllipse (bodyArea.expanded (r * 0.25f).translated (2.0f, 3.5f));

    body.draw (g, bodyArea, [d = diameter] (juce::Graphics& ig, juce::Rectangle<float> area, float scale)
    {
        ig.drawImage (render::knob (d, scale), area);
    });

    // Pointer line on the skirt.
    const float angle = rotaryStart + (float) valueToProportionOfLength (getValue()) * (rotaryEnd - rotaryStart);
    const juce::Line<float> pointer (c.getPointOnCircumference (r * 0.64f, angle), c.getPointOnCircumference (r * 0.82f, angle));
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawLine (pointer.withShortenedStart (-0.5f), 2.6f);
    g.setColour (colours::cream);
    g.drawLine (pointer, 1.8f);
}

// ---------------------------------------------------------------------------
FootSwitch::FootSwitch() : juce::Button ("footswitch")
{
    setClickingTogglesState (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip ("On / bypass");
}

void FootSwitch::paintButton (juce::Graphics& g, bool, bool down)
{
    // The component is larger than the switch so the soft shadow isn't clipped.
    const auto b = getLocalBounds().toFloat();
    const float size = juce::jmin (b.getWidth(), b.getHeight()) - 16.0f;
    const auto area = juce::Rectangle<float> (size, size).withCentre (b.getCentre());

    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.75f), area.getCentreX() + 3.0f, area.getCentreY() + 5.0f,
                                             juce::Colours::transparentBlack, area.getCentreX() + 3.0f + size * 0.62f, area.getCentreY() + 5.0f, true));
    g.fillEllipse (area.expanded (size * 0.12f).translated (3.0f, 5.0f));

    auto& art = down ? pressed : up;
    art.draw (g, area, [size, down] (juce::Graphics& ig, juce::Rectangle<float> a, float scale)
    {
        ig.drawImage (render::footswitch (size, scale, down), a);
    });
}

// ---------------------------------------------------------------------------
MiniToggle::MiniToggle() : juce::Button ("toggle")
{
    setClickingTogglesState (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void MiniToggle::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto c = getLocalBounds().toFloat().getCentre();

    // Hex nut.
    juce::Path nut;
    nut.addPolygon (c, 6, 7.5f, 0.0f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillPath (nut, juce::AffineTransform::translation (1.0f, 2.0f));
    g.setGradientFill (chromeGradient (nut.getBounds()));
    g.fillPath (nut);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.strokePath (nut, juce::PathStrokeType (0.6f));
    g.setColour (Colour (0xff1a1b1d));
    g.fillEllipse (juce::Rectangle<float> (6.5f, 6.5f).withCentre (c));

    // Bat lever, seen from above at an angle: points up when on.
    const bool on = getToggleState();
    const float dir = on ? -1.0f : 1.0f;
    const auto tip = c.translated (0.0f, dir * 12.0f);

    juce::Path shadow;
    shadow.addLineSegment ({ c.translated (2.0f, 3.0f), tip.translated (3.5f, 5.0f) }, 4.0f);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillPath (shadow);

    juce::Path lever;
    lever.startNewSubPath (c.translated (-2.2f, 0.0f));
    lever.lineTo (tip.translated (-1.6f, 0.0f));
    lever.lineTo (tip.translated (1.6f, 0.0f));
    lever.lineTo (c.translated (2.2f, 0.0f));
    lever.closeSubPath();
    juce::ColourGradient metal (Colour (0xff6c727a), c.x - 2.2f, c.y, Colour (0xff3a3e44), c.x + 2.2f, c.y, false);
    metal.addColour (0.35, Colour (0xfff1f4f7));
    g.setGradientFill (metal);
    g.fillPath (lever);

    const auto ball = juce::Rectangle<float> (4.6f, 4.6f).withCentre (tip);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, ball.getX(), ball.getY(), Colour (0xff50555c), ball.getRight(), ball.getBottom(), false));
    g.fillEllipse (ball);

    if (highlighted)
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillEllipse (getLocalBounds().toFloat().reduced (2.0f));
    }
}

// ---------------------------------------------------------------------------
PedalPanel::PedalPanel (juce::AudioProcessorValueTreeState& state, const Pedal& pedal)
    : id (pedal.getId()),
      title (pedal.getName()),
      subtitle (pedal.getSubtitle()),
      onParam (state.getRawParameterValue (pedal.getOnParamId())),
      footswitchAttachment (state, pedal.getOnParamId(), footswitch)
{
    for (const auto& k : pedal.getKnobs())
    {
        Control c;
        c.knob = std::make_unique<RealKnob> (knobBody);
        c.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, k.paramId, *c.knob);
        if (auto* p = state.getParameter (k.paramId))
            c.knob->setDoubleClickReturnValue (true, (double) p->convertFrom0to1 (p->getDefaultValue()));
        c.label = k.label.toUpperCase();
        addAndMakeVisible (*c.knob);
        controls.push_back (std::move (c));
    }

    for (const auto& s : pedal.getSwitches())
    {
        Switch sw;
        sw.toggle = std::make_unique<MiniToggle>();
        sw.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, s.paramId, *sw.toggle);
        std::tie (sw.onLabel, sw.offLabel) = switchLabels (s.paramId, s.label);
        addAndMakeVisible (*sw.toggle);
        switches.push_back (std::move (sw));
    }

    addAndMakeVisible (footswitch);
    ledOn = onParam != nullptr && onParam->load() > 0.5f;
    setSize ((int) pedalWidth, (int) pedalHeight);
    startTimerHz (15);
}

PedalPanel::~PedalPanel() = default;

juce::Point<float> PedalPanel::getInputJack() const  { return { 0.0f, jackY }; }
juce::Point<float> PedalPanel::getOutputJack() const { return { pedalWidth, jackY }; }

void PedalPanel::timerCallback()
{
    const bool on = onParam != nullptr && onParam->load() > 0.5f;
    if (on != ledOn)
    {
        ledOn = on;
        repaint (juce::Rectangle<int> (50, 50).withCentre ({ (int) (pedalWidth * 0.5f), 248 }));
    }
}

void PedalPanel::resized()
{
    // Knob grid: rows of three (two for 2- and 4-knob pedals), centred.
    const int count = (int) controls.size();
    const int perRow = (count == 2 || count == 4) ? 2 : 3;
    const int rows = (count + perRow - 1) / perRow;
    const float spacing = perRow == 3 ? 47.0f : 54.0f;

    for (int i = 0; i < count; ++i)
    {
        const int row = i / perRow;
        const int inRow = juce::jmin (perRow, count - row * perRow);
        const float x = pedalWidth * 0.5f + spacing * ((float) (i % perRow) - (float) (inRow - 1) * 0.5f);
        const float y = rows == 1 ? 60.0f : 44.0f + 62.0f * (float) row;
        controls[(size_t) i].centre = { x, y };
        controls[(size_t) i].knob->setBounds (juce::Rectangle<int> (58, 58).withCentre (juce::Point<float> (x, y).roundToInt()));
    }

    for (size_t i = 0; i < switches.size(); ++i)
    {
        switches[i].centre = { 48.0f + 60.0f * (float) i, 160.0f };
        switches[i].toggle->setBounds (juce::Rectangle<int> (22, 32).withCentre (switches[i].centre.roundToInt()));
    }

    footswitch.setBounds (juce::Rectangle<int> (68, 68).withCentre ({ (int) (pedalWidth * 0.5f), 283 }));
    face.invalidate();
}

void PedalPanel::drawMotif (juce::Graphics& g, juce::Rectangle<float> r) const
{
    juce::Path p;
    const float cy = r.getCentreY(), h = r.getHeight() * 0.5f;
    const auto stroke = juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    auto wave = [&] (std::function<float (float)> fn, int steps = 120)
    {
        for (int i = 0; i <= steps; ++i)
        {
            const float t = (float) i / (float) steps;
            const juce::Point<float> pt (r.getX() + t * r.getWidth(), cy - fn (t) * h);
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
    };

    if (id == "gate")
    {
        wave ([] (float t) { return t < 0.62f ? std::sin (t * 60.0f) * std::exp (-t * 3.0f) : 0.0f; });
        g.strokePath (p, stroke);
        g.drawLine (r.getX() + r.getWidth() * 0.62f, cy - h, r.getX() + r.getWidth() * 0.62f, cy + h, 1.0f);
    }
    else if (id == "od")
    {
        wave ([] (float t) { return juce::jlimit (-0.6f, 0.6f, std::sin (t * 4.0f * juce::MathConstants<float>::twoPi) * 1.4f) / 0.6f; });
        g.strokePath (p, stroke);
    }
    else if (id == "amp")
    {
        const auto grille = r.withSizeKeepingCentre (r.getWidth() * 0.8f, r.getHeight());
        g.drawRoundedRectangle (grille, 3.0f, 1.2f);
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (grille.reduced (2.5f).toNearestInt());
        for (float x = grille.getX() - grille.getHeight(); x < grille.getRight(); x += 4.0f)
        {
            g.drawLine (x, grille.getBottom(), x + grille.getHeight(), grille.getY(), 0.6f);
            g.drawLine (x, grille.getY(), x + grille.getHeight(), grille.getBottom(), 0.6f);
        }
    }
    else if (id == "pre")
    {
        const auto shell = r.withSizeKeepingCentre (r.getWidth() * 0.74f, r.getHeight());
        g.drawRoundedRectangle (shell, 3.0f, 1.2f);
        const float reelR = h * 0.5f;
        for (float fx : { 0.3f, 0.7f })
        {
            const juce::Point<float> c (shell.getX() + shell.getWidth() * fx, cy - h * 0.1f);
            g.drawEllipse (juce::Rectangle<float> (reelR * 2.0f, reelR * 2.0f).withCentre (c), 1.1f);
            for (int s = 0; s < 3; ++s)
                g.drawLine (juce::Line<float> (c, c.getPointOnCircumference (reelR * 0.8f, (float) s * juce::MathConstants<float>::twoPi / 3.0f)), 0.9f);
        }
        g.drawRect (shell.withSizeKeepingCentre (shell.getWidth() * 0.22f, h * 0.55f).translated (0.0f, -h * 0.1f), 0.8f);
        g.drawLine (shell.getX() + shell.getWidth() * 0.18f, shell.getBottom() - 3.0f, shell.getRight() - shell.getWidth() * 0.18f, shell.getBottom() - 3.0f, 0.8f);
    }
    else if (id == "mod")
    {
        wave ([] (float t) { return std::sin (t * 2.5f * juce::MathConstants<float>::twoPi) * 0.9f; });
        g.strokePath (p, stroke);
        p.clear();
        wave ([] (float t) { return std::sin (t * 2.5f * juce::MathConstants<float>::twoPi + 1.3f) * 0.9f; });
        g.strokePath (p, juce::PathStrokeType (0.8f));
    }
    else if (id == "echo")
    {
        for (int i = 0; i < 6; ++i)
        {
            const float x = r.getX() + 8.0f + (float) i * (r.getWidth() - 16.0f) / 5.0f;
            const float a = std::pow (0.68f, (float) i);
            g.drawLine (x, cy + h, x, cy + h - 2.0f * h * a, 2.2f - 0.25f * (float) i);
        }
    }
    else if (id == "spring")
    {
        const int loops = 9;
        for (int i = 0; i < loops; ++i)
        {
            const float x = r.getX() + 6.0f + (float) i * (r.getWidth() - 12.0f) / (float) (loops - 1);
            g.drawEllipse (juce::Rectangle<float> (9.0f, h * 1.8f).withCentre ({ x, cy }), 1.0f);
        }
    }
}

void PedalPanel::renderFace (juce::Graphics& g, juce::Rectangle<float> area, float scale) const
{
    const auto style = styleFor (id);
    g.drawImage (render::enclosure (area.getWidth(), area.getHeight(), scale, { style.paint, style.finish, style.seed }), area);

    const auto ink = style.ink.withAlpha (0.92f);

    // Printed border.
    g.setColour (ink.withAlpha (0.16f));
    g.drawRoundedRectangle (area.reduced (9.0f), 5.0f, 0.8f);

    // Knob scales and labels.
    for (const auto& c : controls)
    {
        g.setColour (ink.withAlpha (0.55f));
        for (int i = 0; i <= 10; ++i)
        {
            const float a = rotaryStart + (float) i / 10.0f * (rotaryEnd - rotaryStart);
            const float dot = (i == 0 || i == 10 || i == 5) ? 1.9f : 1.3f;
            g.fillEllipse (juce::Rectangle<float> (dot, dot).withCentre (c.centre.getPointOnCircumference (knobBody * 0.5f + 4.5f, a)));
        }
        g.setColour (ink);
        g.setFont (sans ("Demi Bold", 7.4f, 0.16f));
        g.drawText (c.label, juce::Rectangle<float> (60.0f, 10.0f).withCentre (c.centre.translated (0.0f, 27.5f)), juce::Justification::centred);
    }

    // Toggle labels.
    for (const auto& s : switches)
    {
        g.setColour (ink);
        g.setFont (sans ("Demi Bold", 7.0f, 0.14f));
        g.drawText (s.onLabel, juce::Rectangle<float> (70.0f, 9.0f).withPosition (s.centre.translated (13.0f, -13.0f)), juce::Justification::centredLeft);
        g.setColour (ink.withAlpha (0.6f));
        g.drawText (s.offLabel, juce::Rectangle<float> (70.0f, 9.0f).withPosition (s.centre.translated (13.0f, 4.0f)), juce::Justification::centredLeft);
    }

    // Jack markings.
    g.setColour (ink.withAlpha (0.55f));
    g.setFont (sans ("Demi Bold", 5.8f, 0.2f));
    g.drawText ("IN", juce::Rectangle<float> (20.0f, 8.0f).withPosition (11.0f, jackY - 4.0f), juce::Justification::centredLeft);
    g.drawText ("OUT", juce::Rectangle<float> (24.0f, 8.0f).withPosition (pedalWidth - 35.0f, jackY - 4.0f), juce::Justification::centredRight);

    // Branding: motif, name, subtitle.
    g.setColour (ink.withAlpha (0.8f));
    drawMotif (g, { 30.0f, 180.0f, pedalWidth - 60.0f, 22.0f });

    g.setColour (ink);
    g.setFont (condensed ("Bold", 19.0f, 0.1f));
    g.drawText (title.toUpperCase(), juce::Rectangle<float> (0.0f, 205.0f, pedalWidth, 20.0f), juce::Justification::centred);
    g.setColour (ink.withAlpha (0.7f));
    g.setFont (sans ("Demi Bold", 6.6f, 0.32f));
    g.drawText (subtitle.toUpperCase(), juce::Rectangle<float> (0.0f, 225.0f, pedalWidth, 9.0f), juce::Justification::centred);
}

void PedalPanel::paint (juce::Graphics& g)
{
    face.draw (g, getLocalBounds().toFloat(), [this] (juce::Graphics& ig, juce::Rectangle<float> a, float scale) { renderFace (ig, a, scale); });

    const juce::Point<float> ledCentre (pedalWidth * 0.5f, 248.0f);
    const auto ledArea = juce::Rectangle<float> (13.0f, 13.0f).withCentre (ledCentre);

    if (ledOn)
    {
        g.setGradientFill (juce::ColourGradient (colours::ledRed.withAlpha (0.5f), ledCentre.x, ledCentre.y,
                                                 colours::ledRed.withAlpha (0.0f), ledCentre.x + 20.0f, ledCentre.y, true));
        g.fillEllipse (ledArea.expanded (14.0f));
    }

    (ledOn ? ledLit : ledDark).draw (g, ledArea, [lit = ledOn] (juce::Graphics& ig, juce::Rectangle<float> a, float scale)
    {
        ig.drawImage (render::led (a.getWidth(), scale, lit), a);
    });
}

// ---------------------------------------------------------------------------
PresetDisplay::StepButton::StepButton (bool isNext) : juce::Button (isNext ? "next" : "previous"), next (isNext)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip (isNext ? "Next preset" : "Previous preset");
}

void PresetDisplay::StepButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const auto disc = juce::Rectangle<float> (24.0f, 24.0f).withCentre (c);

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillEllipse (disc.translated (1.0f, 2.0f));
    g.setGradientFill (chromeGradient (disc));
    g.fillEllipse (disc);
    g.setColour (juce::Colours::black.withAlpha (down ? 0.3f : (highlighted ? 0.0f : 0.08f)));
    g.fillEllipse (disc);
    g.setColour (juce::Colours::black.withAlpha (0.65f));
    g.drawEllipse (disc, 0.8f);

    juce::Path arrow;
    const float s = next ? 1.0f : -1.0f;
    arrow.addTriangle (c.x - 3.0f * s, c.y - 4.5f, c.x - 3.0f * s, c.y + 4.5f, c.x + 4.0f * s, c.y);
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.fillPath (arrow, juce::AffineTransform::translation (0.0f, 0.8f));
    g.setColour (Colour (0xff1c1d20));
    g.fillPath (arrow);
}

PresetDisplay::PresetDisplay (OrionAudioProcessor& p) : processor (p)
{
    addAndMakeVisible (previous);
    addAndMakeVisible (next);
    previous.onClick = [this] { step (-1); };
    next.onClick = [this] { step (1); };
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    refreshUserPresets();
    shownChangeCount = processor.getPresetChangeCount();
    startTimerHz (10);
}

void PresetDisplay::timerCallback()
{
    if (const auto changeCount = processor.getPresetChangeCount(); changeCount != shownChangeCount)
    {
        shownChangeCount = changeCount;
        refreshUserPresets();
        repaint();
    }
    else if (processor.isPresetModified() != shownModified)
    {
        repaint();
    }
}

void PresetDisplay::refreshUserPresets()
{
    userPresets.clear();
    for (const auto& preset : processor.getUserPresets().scan())
        userPresets.push_back (preset.name);
}

int PresetDisplay::getCurrentIndex() const
{
    const auto current = processor.getCurrentPreset();
    if (! current.isUser)
        return findFactoryPreset (current.name);

    for (size_t i = 0; i < userPresets.size(); ++i)
        if (userPresets[i].equalsIgnoreCase (current.name))   // macOS file names ignore case
            return (int) (getFactoryPresets().size() + i);

    return -1;
}

void PresetDisplay::loadIndex (int index)
{
    const int numFactory = (int) getFactoryPresets().size();

    if (index < numFactory)
        processor.loadFactoryPreset (index);
    else if (juce::isPositiveAndBelow (index - numFactory, (int) userPresets.size())
             && ! processor.loadUserPreset (userPresets[(size_t) (index - numFactory)]))
        refreshUserPresets();   // deleted or renamed outside Orion Spark

    repaint();
}

void PresetDisplay::step (int delta)
{
    refreshUserPresets();
    const int count = (int) (getFactoryPresets().size() + userPresets.size());
    const int current = getCurrentIndex();

    if (current < 0)
        loadIndex (delta > 0 ? 0 : count - 1);
    else
        loadIndex ((current + delta + count) % count);
}

juce::Rectangle<float> PresetDisplay::getWindowArea() const
{
    return getLocalBounds().toFloat().reduced (36.0f, 2.0f);
}

void PresetDisplay::resized()
{
    previous.setBounds (getLocalBounds().removeFromLeft (32));
    next.setBounds (getLocalBounds().removeFromRight (32));
}

void PresetDisplay::mouseUp (const juce::MouseEvent& e)
{
    if (getWindowArea().contains (e.position))
        showMenu();
}

void PresetDisplay::showMenu()
{
    enum MenuIds { save = 1, saveAsId, renameId, deleteId, showFolder, firstPreset = 1000 };

    refreshUserPresets();
    const auto current = processor.getCurrentPreset();
    const int currentIndex = getCurrentIndex();
    const auto& factory = getFactoryPresets();
    const int numFactory = (int) factory.size();

    juce::PopupMenu menu;
    juce::String category;
    for (int i = 0; i < numFactory; ++i)
    {
        if (factory[(size_t) i].category != category)
        {
            category = factory[(size_t) i].category;
            menu.addSectionHeader (category.toUpperCase());
        }
        menu.addItem (firstPreset + i, factory[(size_t) i].name, true, i == currentIndex);
    }

    menu.addSectionHeader (juce::String (UserPresetLibrary::category).toUpperCase());
    for (int i = 0; i < (int) userPresets.size(); ++i)
        menu.addItem (firstPreset + numFactory + i, userPresets[(size_t) i], true, numFactory + i == currentIndex);
    if (userPresets.empty())
        menu.addItem (-1, "No saved presets yet", false);

    // Factory presets are read-only; their tweaks go to a new preset with Save As.
    const bool userPresetExists = current.isUser && currentIndex >= 0;
    menu.addSeparator();
    menu.addItem (save, current.isUser ? "Save \"" + current.name + "\"" : juce::String ("Save"), current.isUser);
    menu.addItem (saveAsId, "Save As...");
    menu.addItem (renameId, "Rename...", userPresetExists);
    menu.addItem (deleteId, "Delete", userPresetExists);
    menu.addSeparator();
    menu.addItem (showFolder, "Show Presets Folder");

    juce::Component::SafePointer<PresetDisplay> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth() - 72),
                        [safe, current] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;

                            switch (result)
                            {
                                case save:     safe->saveCurrent(); break;
                                case renameId: safe->askForName ("Rename Preset", current.name, "Rename",
                                                                 [safe] (const juce::String& name) { if (safe != nullptr) safe->renameCurrent (name); });
                                               break;
                                case saveAsId: safe->askForName ("Save Preset", current.name, "Save",
                                                                 [safe] (const juce::String& name) { if (safe != nullptr) safe->saveAs (name); });
                                               break;
                                case deleteId: safe->deleteCurrent(); break;
                                case showFolder:
                                {
                                    const auto folder = safe->processor.getUserPresets().getFolder();
                                    folder.createDirectory();
                                    folder.startAsProcess();   // opens it in Finder
                                    break;
                                }
                                default:       safe->loadIndex (result - firstPreset); break;
                            }
                        });
}

void PresetDisplay::saveCurrent()
{
    const auto current = processor.getCurrentPreset();
    if (current.isUser && ! processor.saveUserPreset (current.name))
        showMessage ("Couldn't Save Preset", "\"" + current.name + "\" could not be written to\n"
                                                 + processor.getUserPresets().getFolder().getFullPathName());
    repaint();
}

void PresetDisplay::saveAs (const juce::String& name)
{
    const auto cleaned = UserPresetLibrary::cleanName (name);
    if (cleaned.isEmpty())
    {
        showMessage ("Save Preset", "Please enter a name.");
        return;
    }

    juce::Component::SafePointer<PresetDisplay> safe (this);
    auto write = [safe, cleaned]
    {
        if (safe == nullptr)
            return;
        if (! safe->processor.saveUserPreset (cleaned))
            safe->showMessage ("Couldn't Save Preset", "\"" + cleaned + "\" could not be written to\n"
                                                           + safe->processor.getUserPresets().getFolder().getFullPathName());
        safe->repaint();
    };

    if (processor.getUserPresets().exists (cleaned))
        confirm ("Replace Preset?", "A preset named \"" + cleaned + "\" already exists. Replace it?", "Replace", write);
    else
        write();
}

void PresetDisplay::renameCurrent (const juce::String& newName)
{
    const auto current = processor.getCurrentPreset();
    const auto cleaned = UserPresetLibrary::cleanName (newName);

    if (cleaned.isEmpty())
        showMessage ("Rename Preset", "Please enter a name.");
    else if (cleaned == current.name)
        return;
    else if (processor.getUserPresets().exists (cleaned) && ! cleaned.equalsIgnoreCase (current.name))
        showMessage ("Rename Preset", "A preset named \"" + cleaned + "\" already exists.");
    else if (! processor.renameUserPreset (current.name, cleaned))
        showMessage ("Couldn't Rename Preset", "\"" + current.name + "\" could not be renamed.");

    repaint();
}

void PresetDisplay::deleteCurrent()
{
    const auto name = processor.getCurrentPreset().name;
    juce::Component::SafePointer<PresetDisplay> safe (this);

    confirm ("Delete Preset?", "\"" + name + "\" will be moved to the Trash.", "Delete",
             [safe, name]
             {
                 if (safe == nullptr)
                     return;
                 if (! safe->processor.deleteUserPreset (name))
                     safe->showMessage ("Couldn't Delete Preset", "\"" + name + "\" could not be moved to the Trash.");
                 safe->repaint();
             });
}

juce::Component& PresetDisplay::getDialogParent()
{
    // The root is scaled with the window, so dialogs scale with it.
    auto* parent = getParentComponent();
    return parent != nullptr ? *parent : *this;
}

void PresetDisplay::askForName (const juce::String& title, const juce::String& initialName, const juce::String& confirmText,
                                std::function<void (const juce::String&)> onConfirm)
{
    // Its own small window rather than a child of the editor: the VST3 wrapper makes the
    // plugin window ignore key presses (EDITOR_WANTS_KEYBOARD_FOCUS is off), which would
    // stop the name from being typed.
    nameDialog = std::make_unique<juce::AlertWindow> (title, juce::String(), juce::MessageBoxIconType::NoIcon, this);
    nameDialog->setLookAndFeel (&getLookAndFeel());
    nameDialog->setAlwaysOnTop (true);
    nameDialog->addTextEditor ("name", initialName, "Name:");
    nameDialog->addButton (confirmText, 1, juce::KeyPress (juce::KeyPress::returnKey));
    nameDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    if (auto* editor = nameDialog->getTextEditor ("name"))
    {
        editor->setInputRestrictions (UserPresetLibrary::maxNameLength);
        editor->selectAll();
    }

    juce::Component::SafePointer<PresetDisplay> safe (this);
    nameDialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe, callback = std::move (onConfirm)] (int result)
    {
        if (safe == nullptr || safe->nameDialog == nullptr)
            return;

        const auto name = safe->nameDialog->getTextEditorContents ("name");
        safe->nameDialog.reset();

        if (result == 1)
            callback (name);
    }), false);
}

void PresetDisplay::confirm (const juce::String& title, const juce::String& message, const juce::String& confirmText,
                             std::function<void()> onConfirm)
{
    messageBox = juce::AlertWindow::showScopedAsync (juce::MessageBoxOptions()
                                                         .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                                         .withTitle (title)
                                                         .withMessage (message)
                                                         .withButton (confirmText, 1)
                                                         .withButton ("Cancel", 0)
                                                         .withParentComponent (&getDialogParent()),
                                                     [callback = std::move (onConfirm)] (int result)
                                                     {
                                                         if (result == 1)
                                                             callback();
                                                     });
}

void PresetDisplay::showMessage (const juce::String& title, const juce::String& message)
{
    messageBox = juce::AlertWindow::showScopedAsync (juce::MessageBoxOptions()
                                                         .withIconType (juce::MessageBoxIconType::WarningIcon)
                                                         .withTitle (title)
                                                         .withMessage (message)
                                                         .withButton ("OK", 0)
                                                         .withParentComponent (&getDialogParent()),
                                                     nullptr);
}

void PresetDisplay::paint (juce::Graphics& g)
{
    const auto current = processor.getCurrentPreset();
    const int index = getCurrentIndex();
    const int factoryIndex = current.isUser ? -1 : findFactoryPreset (current.name);
    const auto category = current.isUser ? juce::String (UserPresetLibrary::category)
                                         : (factoryIndex >= 0 ? getFactoryPresets()[(size_t) factoryIndex].category : juce::String());
    shownModified = processor.isPresetModified();
    const auto w = getWindowArea();

    // Recessed bezel.
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.fillRoundedRectangle (w.translated (0.0f, 1.0f), 6.0f);
    g.setColour (juce::Colours::black.withAlpha (0.9f));
    g.fillRoundedRectangle (w.translated (0.0f, -0.6f), 6.0f);

    // Smoked glass.
    const auto glass = w.reduced (1.5f);
    g.setGradientFill (juce::ColourGradient (Colour (0xff100c08), glass.getX(), glass.getY(), Colour (0xff050403), glass.getX(), glass.getBottom(), false));
    g.fillRoundedRectangle (glass, 5.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.7f), glass.getX(), glass.getY(),
                                             juce::Colours::transparentBlack, glass.getX(), glass.getY() + 8.0f, false));
    g.fillRoundedRectangle (glass, 5.0f);

    // Backlit text with a soft glow.
    auto text = glass.reduced (14.0f, 0.0f);
    const auto indexArea = text.removeFromLeft (26.0f);
    const auto categoryArea = text.removeFromRight (110.0f);

    const auto glowText = [&g] (const juce::String& s, juce::Rectangle<float> r, juce::Justification j, Colour c)
    {
        g.setColour (c.withAlpha (0.12f));
        for (auto o : std::initializer_list<juce::Point<float>> { { -1.2f, 0.0f }, { 1.2f, 0.0f }, { 0.0f, -1.2f }, { 0.0f, 1.2f } })
            g.drawText (s, r.translated (o.x, o.y), j);
        g.setColour (c);
        g.drawText (s, r, j);
    };

    g.setFont (sans ("Demi Bold", 10.0f, 0.1f));
    glowText (index >= 0 ? juce::String (index + 1).paddedLeft ('0', 2) : juce::String ("--"), indexArea,
              juce::Justification::centredLeft, colours::amber.withAlpha (0.55f));
    g.setFont (sans ("Demi Bold", 17.0f, 0.04f));
    glowText (current.name + (shownModified ? " *" : ""), text, juce::Justification::centredLeft, colours::amber);
    g.setFont (sans ("Demi Bold", 8.0f, 0.22f));
    glowText (category.toUpperCase() + juce::String (juce::CharPointer_UTF8 ("  \xe2\x96\xbe")), categoryArea,
              juce::Justification::centredRight, colours::amber.withAlpha (0.6f));

    // Glass reflection.
    juce::Path sheen;
    sheen.addRoundedRectangle (glass.withHeight (glass.getHeight() * 0.45f), 5.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.07f), glass.getX(), glass.getY(),
                                             juce::Colours::white.withAlpha (0.0f), glass.getX(), glass.getY() + glass.getHeight() * 0.45f, false));
    g.fillPath (sheen);
}

// ---------------------------------------------------------------------------
EditorRoot::EditorRoot (OrionAudioProcessor& p)
    : presetDisplay (p),
      inputAttachment (p.getState(), OrionAudioProcessor::inputGainId, inputKnob),
      outputAttachment (p.getState(), OrionAudioProcessor::outputGainId, outputKnob)
{
    addAndMakeVisible (presetDisplay);
    addAndMakeVisible (inputKnob);
    addAndMakeVisible (outputKnob);

    for (auto* knob : { &inputKnob, &outputKnob })
        knob->setDoubleClickReturnValue (true, 0.0);

    for (const auto& pedal : p.getPedals())
        addAndMakeVisible (*panels.emplace_back (std::make_unique<PedalPanel> (p.getState(), *pedal)));

    setSize (designWidth, designHeight);
}

void EditorRoot::resized()
{
    presetDisplay.setBounds (juce::Rectangle<int> (480, 46).withCentre ({ designWidth / 2, headerHeight / 2 }));
    inputKnob.setBounds (juce::Rectangle<int> (56, 56).withCentre ({ designWidth - 150, 32 }));
    outputKnob.setBounds (juce::Rectangle<int> (56, 56).withCentre ({ designWidth - 72, 32 }));

    const int count = (int) panels.size();
    const int left = (designWidth - count * slotWidth) / 2;
    const int top = headerHeight + 14 + (designHeight - headerHeight - 28 - (int) pedalHeight) / 2 + 12;

    for (int i = 0; i < count; ++i)
        panels[(size_t) i]->setTopLeftPosition (left + i * slotWidth + (slotWidth - (int) pedalWidth) / 2, top);

    header.invalidate();
    boardArt.invalidate();
}

void EditorRoot::renderHeader (juce::Graphics& g, juce::Rectangle<float> area, float scale) const
{
    g.drawImage (render::brushedPlate (area.getWidth(), area.getHeight(), scale), area);

    for (auto pt : std::initializer_list<juce::Point<float>> { { 13.0f, 13.0f }, { area.getRight() - 13.0f, 13.0f },
                                                              { 13.0f, area.getBottom() - 13.0f }, { area.getRight() - 13.0f, area.getBottom() - 13.0f } })
        drawScrew (g, pt, 4.6f, pt.x * 0.37f + pt.y * 0.11f);

    drawOrionSparkMark (g, { 64.0f, area.getCentreY() }, 17.0f);

    g.setFont (sans ("Heavy", 19.0f, 0.34f));
    drawPrinted (g, "ORION SPARK", { 98.0f, 20.0f, 300.0f, 24.0f }, juce::Justification::centredLeft, colours::cream);

    g.setFont (sans ("Demi Bold", 7.0f, 0.42f));
    drawPrinted (g, juce::String (juce::CharPointer_UTF8 ("TAPE \xc2\xb7 DRIVE \xc2\xb7 SPACE")),
                 { 99.0f, 44.0f, 300.0f, 10.0f }, juce::Justification::centredLeft, colours::cream.withAlpha (0.45f));

    g.setFont (sans ("Demi Bold", 7.0f, 0.3f));
    for (auto [label, x] : { std::pair { "INPUT", (float) designWidth - 150.0f }, std::pair { "OUTPUT", (float) designWidth - 72.0f } })
        drawPrinted (g, label, juce::Rectangle<float> (70.0f, 10.0f).withCentre ({ x, 63.0f }), juce::Justification::centred, colours::cream.withAlpha (0.6f));
}

void EditorRoot::renderBoard (juce::Graphics& g, juce::Rectangle<float> area, float scale) const
{
    // Work in root coordinates.
    g.setOrigin ({ 0, -headerHeight });
    const auto boardArea = juce::Rectangle<float> (14.0f, (float) headerHeight + 14.0f, (float) designWidth - 28.0f,
                                                   (float) designHeight - (float) headerHeight - 28.0f);

    g.drawImage (render::board (area.getWidth(), area.getHeight(), scale, boardArea.translated (0.0f, (float) -headerHeight)),
                 area.translated (0.0f, (float) headerHeight));

    // Pedal shadows on the board: a wide soft one plus a tight contact shadow.
    for (const auto& panel : panels)
    {
        const auto r = panel->getBounds().toFloat();
        const auto shape = roundedRect (r, 10.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.8f), 22, { 9, 15 }).drawForPath (g, shape);
        juce::DropShadow (juce::Colours::black.withAlpha (0.85f), 5, { 1, 3 }).drawForPath (g, shape);
    }

    const auto cable = [&g] (const juce::Path& path, float width, Colour colour)
    {
        juce::Path outline;
        juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (outline, path);
        juce::DropShadow (juce::Colours::black.withAlpha (0.65f), 7, { 3, 6 }).drawForPath (g, outline);
        g.setColour (colour);
        g.fillPath (outline);
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.strokePath (path, juce::PathStrokeType (width * 0.35f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                      juce::AffineTransform::translation (width * 0.18f, width * 0.2f));
        g.setColour (juce::Colours::white.withAlpha (0.13f));
        g.strokePath (path, juce::PathStrokeType (width * 0.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                      juce::AffineTransform::translation (-width * 0.16f, -width * 0.2f));
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.strokePath (path, juce::PathStrokeType (width * 0.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                      juce::AffineTransform::translation (-width * 0.22f, -width * 0.27f));
    };

    // Right-angle "pancake" plug at a side jack; returns where its cable leaves.
    const auto plug = [&g] (juce::Point<float> jack, bool rightSide)
    {
        const auto body = juce::Rectangle<float> (9.0f, 17.0f).withPosition (rightSide ? jack.x : jack.x - 9.0f, jack.y - 8.5f);
        const auto boot = juce::Rectangle<float> (6.5f, 13.0f).withCentre ({ body.getCentreX(), body.getBottom() + 5.0f });
        juce::DropShadow (juce::Colours::black.withAlpha (0.7f), 5, { 2, 4 }).drawForRectangle (g, body.getUnion (boot).toNearestInt());

        g.setGradientFill (juce::ColourGradient (Colour (0xff2a2b2e), boot.getX(), boot.getY(), Colour (0xff050506), boot.getRight(), boot.getY(), false));
        g.fillRoundedRectangle (boot, 2.5f);
        juce::ColourGradient metal (Colour (0xff3a3e44), body.getX(), body.getY(), Colour (0xff1c1e21), body.getRight(), body.getY(), false);
        metal.addColour (0.3, Colour (0xffc9ced4));
        metal.addColour (0.5, Colour (0xff7d838b));
        g.setGradientFill (metal);
        g.fillRoundedRectangle (body, 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (body, 2.0f, 0.6f);
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.drawLine (body.getX() + 1.5f, body.getY() + 1.2f, body.getRight() - 1.5f, body.getY() + 1.2f, 0.7f);
        return juce::Point<float> (boot.getCentreX(), boot.getBottom() - 1.0f);
    };

    const Colour cableColour (0xff131315);

    // Power: a daisy-chain bus along the top of the board with a drop to each pedal.
    {
        const float busY = (float) panels.front()->getY() - 30.0f;
        juce::Path bus;
        bus.startNewSubPath (-20.0f, busY + 10.0f);
        float prevX = -20.0f;
        for (size_t i = 0; i < panels.size(); ++i)
        {
            const float x = (float) panels[i]->getBounds().getCentreX();
            const float sag = (i % 2 == 0) ? 4.0f : -2.0f;
            bus.cubicTo (prevX + (x - prevX) * 0.4f, busY + sag + 6.0f, prevX + (x - prevX) * 0.6f, busY + sag + 6.0f, x, busY + sag);
            prevX = x;
        }
        cable (bus, 3.4f, cableColour);

        for (size_t i = 0; i < panels.size(); ++i)
        {
            const auto r = panels[i]->getBounds().toFloat();
            const float sag = (i % 2 == 0) ? 4.0f : -2.0f;
            const auto dc = juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ r.getCentreX(), r.getY() - 4.0f });
            juce::Path drop;
            drop.startNewSubPath (r.getCentreX(), busY + sag);
            drop.cubicTo (r.getCentreX() + 8.0f, busY + sag + 8.0f, r.getCentreX(), dc.getY() - 8.0f, r.getCentreX(), dc.getY());
            cable (drop, 3.0f, cableColour);

            juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 4, { 1, 3 }).drawForRectangle (g, dc.toNearestInt());
            g.setGradientFill (juce::ColourGradient (Colour (0xff35363a), dc.getX(), dc.getY(), Colour (0xff09090a), dc.getX(), dc.getBottom(), false));
            g.fillRoundedRectangle (dc, 2.5f);
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.drawRoundedRectangle (dc.reduced (0.5f), 2.5f, 0.6f);
        }
    }

    // Signal: input cable, patch cables between pedals, output cable.
    std::vector<juce::Point<float>> outs, ins;
    for (const auto& panel : panels)
    {
        const auto origin = panel->getPosition().toFloat();
        ins.push_back (plug (origin + panel->getInputJack(), false));
        outs.push_back (plug (origin + panel->getOutputJack(), true));
    }

    for (size_t i = 0; i + 1 < panels.size(); ++i)
    {
        juce::Path patch;
        patch.startNewSubPath (outs[i]);
        patch.cubicTo (outs[i].translated (0.0f, 62.0f), ins[i + 1].translated (0.0f, 62.0f), ins[i + 1]);
        cable (patch, 5.0f, cableColour);
    }

    {
        const auto start = ins.front();
        juce::Path in;
        in.startNewSubPath (start);
        in.cubicTo (start.translated (0.0f, 80.0f), { boardArea.getX() + 20.0f, start.y + 150.0f }, { -30.0f, start.y + 170.0f });
        cable (in, 6.5f, Colour (0xff1b1a19));

        const auto end = outs.back();
        juce::Path out;
        out.startNewSubPath (end);
        out.cubicTo (end.translated (0.0f, 80.0f), { boardArea.getRight() - 20.0f, end.y + 150.0f }, { (float) designWidth + 30.0f, end.y + 170.0f });
        cable (out, 6.5f, Colour (0xff1b1a19));
    }

    // Maker's nameplate, bottom centre of the board.
    const float pedalsBottom = (float) panels.front()->getBottom();
    const float plateY = pedalsBottom + 21.0f;
    renderNameplate (g, juce::Rectangle<float> (284.0f, 22.0f).withCentre ({ boardArea.getCentreX(), plateY }));
}

void EditorRoot::renderNameplate (juce::Graphics& g, juce::Rectangle<float> plate) const
{
    // Brushed brass plate screwed to the board.
    juce::DropShadow (juce::Colours::black.withAlpha (0.75f), 6, { 2, 3 }).drawForPath (g, roundedRect (plate, 3.0f));

    juce::ColourGradient brass (Colour (0xffcfb070), plate.getX(), plate.getY(), Colour (0xff5e4519), plate.getX(), plate.getBottom(), false);
    brass.addColour (0.35, Colour (0xffae8c4b));
    brass.addColour (0.6, Colour (0xff8d6b33));
    g.setGradientFill (brass);
    g.fillRoundedRectangle (plate, 3.0f);

    {
        // Fine horizontal brushing.
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (roundedRect (plate, 3.0f));
        juce::Random rng (0xb4a55);
        for (float y = plate.getY(); y < plate.getBottom(); y += 0.5f)
        {
            g.setColour ((rng.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (rng.nextFloat() * 0.07f));
            g.drawHorizontalLine ((int) y, plate.getX(), plate.getRight());
        }
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.28f), plate.getX() + plate.getWidth() * 0.3f, plate.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), plate.getX() + plate.getWidth() * 0.55f, plate.getBottom(), false));
        g.fillRect (plate);
    }

    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.drawLine (plate.getX() + 3.0f, plate.getY() + 0.7f, plate.getRight() - 3.0f, plate.getY() + 0.7f, 0.6f);
    g.setColour (Colour (0xff3a2a10).withAlpha (0.8f));
    g.drawRoundedRectangle (plate, 3.0f, 0.7f);

    drawScrew (g, { plate.getX() + 9.0f, plate.getCentreY() }, 3.2f, 0.6f);
    drawScrew (g, { plate.getRight() - 9.0f, plate.getCentreY() }, 3.2f, 2.1f);

    // Engraved lettering: dark cut with a bright lower lip, as on real engraved brass.
    const auto engrave = [&g] (const juce::String& text, juce::Rectangle<float> r, juce::Justification j)
    {
        g.setColour (Colour (0xfff7e4b8).withAlpha (0.7f));
        g.drawText (text, r.translated (0.0f, 0.6f), j);
        g.setColour (Colour (0xff2e2108));
        g.drawText (text, r, j);
    };

    auto text = plate.reduced (22.0f, 0.0f);
    const juce::String lead ("DESIGNED & ENGINEERED BY");
    const auto leadFont = sans ("Demi Bold", 6.2f, 0.24f);
    const auto scriptFont = font ("Snell Roundhand", "Bold", 15.5f);
    const float leadWidth = juce::GlyphArrangement::getStringWidth (leadFont, lead);
    const float scriptWidth = juce::GlyphArrangement::getStringWidth (scriptFont, "Jonathan Neroth");
    const float gap = 7.0f;
    auto line = text.withSizeKeepingCentre (leadWidth + gap + scriptWidth, text.getHeight());

    g.setFont (leadFont);
    engrave (lead, line.removeFromLeft (leadWidth).translated (0.0f, 0.8f), juce::Justification::centredLeft);
    line.removeFromLeft (gap);
    g.setFont (scriptFont);
    engrave ("Jonathan Neroth", line.translated (0.0f, -0.5f), juce::Justification::centredLeft);
}

void EditorRoot::paint (juce::Graphics& g)
{
    const auto headerArea = juce::Rectangle<float> (0.0f, 0.0f, (float) designWidth, (float) headerHeight);
    const auto boardRegion = juce::Rectangle<float> (0.0f, (float) headerHeight, (float) designWidth, (float) (designHeight - headerHeight));

    boardArt.draw (g, boardRegion, [this] (juce::Graphics& ig, juce::Rectangle<float> a, float scale) { renderBoard (ig, a, scale); });
    header.draw (g, headerArea, [this] (juce::Graphics& ig, juce::Rectangle<float> a, float scale) { renderHeader (ig, a, scale); });

    // The header plate casts a shadow onto the board.
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.6f), 0.0f, (float) headerHeight,
                                             juce::Colours::transparentBlack, 0.0f, (float) headerHeight + 14.0f, false));
    g.fillRect (boardRegion.withHeight (14.0f));
}

void EditorRoot::paintOverChildren (juce::Graphics& g)
{
    // Pool of warm light on the board, falling off into darkness at the edges.
    const auto board = juce::Rectangle<float> (0.0f, (float) headerHeight, (float) designWidth, (float) (designHeight - headerHeight));
    const auto c = juce::Point<float> (board.getCentreX() - 60.0f, board.getY() + board.getHeight() * 0.42f);
    juce::ColourGradient pool (juce::Colours::transparentBlack, c.x, c.y, juce::Colours::black.withAlpha (0.62f), board.getX() - 40.0f, board.getBottom(), true);
    pool.addColour (0.55, juce::Colours::black.withAlpha (0.08f));
    g.setGradientFill (pool);
    g.fillRect (board);

    g.setGradientFill (juce::ColourGradient (Colour (0x14ffb060), c.x, c.y, Colour (0x00ffb060), c.x + 520.0f, c.y, true));
    g.fillRect (board);
}

} // namespace orion

// ---------------------------------------------------------------------------
OrionAudioProcessorEditor::OrionAudioProcessorEditor (OrionAudioProcessor& p)
    : AudioProcessorEditor (&p), root (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (root);

    constexpr int w = orion::EditorRoot::designWidth, h = orion::EditorRoot::designHeight;
    setResizable (true, true);
    setResizeLimits ((int) (w * 0.6), (int) (h * 0.6), (int) (w * 1.6), (int) (h * 1.6));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) w / (double) h);
    setSize (w, h);
}

OrionAudioProcessorEditor::~OrionAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void OrionAudioProcessorEditor::resized()
{
    const float s = (float) getWidth() / (float) orion::EditorRoot::designWidth;
    root.setTransform (juce::AffineTransform::scale (s));
}
