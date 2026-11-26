#include "Logo.h"

namespace orion
{

namespace
{
    using juce::Colour;

    const Colour white (0xffffffff);
    const Colour iceBlue (0xff9cc2ff);     // the real belt stars are blue supergiants
    const Colour amber (0xffffa448), gold (0xffffd592);

    struct Star
    {
        juce::Point<float> offset;   // in units of 'size'
        float scale;
        bool spark;
    };

    // Alnitak, Alnilam (the spark), Mintaka: a slightly tilted, slightly bent line.
    const Star belt[] = {
        { { -1.0f,  0.40f }, 0.55f, false },
        { {  0.0f,  0.0f  }, 1.0f,  true  },
        { {  1.0f, -0.36f }, 0.5f,  false },
    };

    // A thin diffraction spike: a tapered diamond, bright at the star and fading to its tip.
    void spike (juce::Graphics& g, juce::Point<float> p, float length, float width, float angle, Colour c, float alpha)
    {
        juce::Path d;
        d.startNewSubPath (-length, 0.0f);
        d.lineTo (0.0f, -width * 0.5f);
        d.lineTo (length, 0.0f);
        d.lineTo (0.0f, width * 0.5f);
        d.closeSubPath();
        d.applyTransform (juce::AffineTransform::rotation (angle).translated (p));

        juce::ColourGradient fade (c.withAlpha (alpha), p.x, p.y, c.withAlpha (0.0f), p.x + length, p.y, true);
        fade.addColour (0.25, c.withAlpha (alpha * 0.55f));
        g.setGradientFill (fade);
        g.fillPath (d);
    }

    void drawStar (juce::Graphics& g, juce::Point<float> p, float size, const Star& s, bool withCore)
    {
        const float k = size * s.scale;
        const Colour tint = s.spark ? amber : iceBlue;
        const float pi = juce::MathConstants<float>::pi;

        // Halo.
        juce::ColourGradient halo (tint.withAlpha (s.spark ? 0.7f : 0.55f), p.x, p.y, tint.withAlpha (0.0f), p.x + k * 0.8f, p.y, true);
        halo.addColour (0.3, tint.withAlpha (s.spark ? 0.3f : 0.2f));
        g.setGradientFill (halo);
        g.fillEllipse (juce::Rectangle<float> (k * 1.6f, k * 1.6f).withCentre (p));

        // Four-point diffraction spikes (the spark also gets fainter diagonals).
        const float len = k * (s.spark ? 1.35f : 0.95f);
        const float w = k * 0.11f;
        spike (g, p, len, w, 0.0f, s.spark ? gold : white, 0.95f);
        spike (g, p, len * 0.8f, w, pi * 0.5f, s.spark ? gold : white, 0.9f);
        if (s.spark)
        {
            spike (g, p, len * 0.45f, w * 0.7f, pi * 0.25f, gold, 0.6f);
            spike (g, p, len * 0.45f, w * 0.7f, -pi * 0.25f, gold, 0.6f);
        }

        if (withCore)
        {
            const float cr = k * (s.spark ? 0.17f : 0.13f);
            juce::ColourGradient core (white, p.x, p.y, (s.spark ? gold : iceBlue).withAlpha (0.0f), p.x + cr * 1.6f, p.y, true);
            core.addColour (0.55, white);
            g.setGradientFill (core);
            g.fillEllipse (juce::Rectangle<float> (cr * 3.2f, cr * 3.2f).withCentre (p));
        }
    }

    void drawLight (juce::Graphics& g, juce::Point<float> c, float size, bool withCores)
    {
        // A faint anamorphic streak through the spark, like a lens catching it.
        const float streak = size * 1.75f;
        juce::ColourGradient line (amber.withAlpha (0.0f), c.x - streak, c.y, amber.withAlpha (0.0f), c.x + streak, c.y, false);
        line.addColour (0.5, gold.withAlpha (0.55f));
        line.addColour (0.3, amber.withAlpha (0.18f));
        line.addColour (0.7, amber.withAlpha (0.18f));
        g.setGradientFill (line);
        g.fillEllipse (juce::Rectangle<float> (streak * 2.0f, size * 0.07f).withCentre (c));

        for (const auto& s : belt)
            drawStar (g, c + s.offset * size, size, s, withCores);
    }
}

void drawOrionSparkMark (juce::Graphics& g, juce::Point<float> c, float size)
{
    // Bloom: render the light at low resolution, blur it, and lay it underneath
    // (and a little on top), so the stars glow rather than just sit there.
    constexpr int glowPixels = 128;
    const float extent = size * 4.0f;
    const float q = (float) glowPixels / extent;

    juce::Image light (juce::Image::ARGB, glowPixels, glowPixels, true);
    {
        juce::Graphics lg (light);
        lg.addTransform (juce::AffineTransform::scale (q));
        drawLight (lg, { extent * 0.5f, extent * 0.5f }, size, true);
    }
    juce::Image glow (juce::Image::ARGB, glowPixels, glowPixels, true);
    juce::ImageConvolutionKernel kernel (15);
    kernel.createGaussianBlur (4.0f);
    kernel.applyToImage (glow, light, light.getBounds());

    const auto glowArea = juce::Rectangle<float> (extent, extent).withCentre (c);
    g.setOpacity (1.0f);
    g.drawImage (glow, glowArea, juce::RectanglePlacement::stretchToFit);

    drawLight (g, c, size, true);

    g.setOpacity (0.35f);
    g.drawImage (glow, glowArea, juce::RectanglePlacement::stretchToFit);
    g.setOpacity (1.0f);
}

} // namespace orion
