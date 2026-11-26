#pragma once

#include <juce_graphics/juce_graphics.h>
#include <cmath>

// Procedural, physically-inspired rendering of Orion Spark's hardware: every surface
// is shaded per pixel from a height/normal model, a fixed studio light set and
// an environment map (for chrome). Results are cached by the UI, so this only
// runs when the window opens or changes size.
//
// All functions take sizes in points plus the display scale, and return images
// at physical-pixel resolution.
namespace orion::render
{

struct V3
{
    float x = 0, y = 0, z = 0;
    V3 operator+ (V3 o) const noexcept  { return { x + o.x, y + o.y, z + o.z }; }
    V3 operator- (V3 o) const noexcept  { return { x - o.x, y - o.y, z - o.z }; }
    V3 operator* (float s) const noexcept { return { x * s, y * s, z * s }; }
    V3 operator* (V3 o) const noexcept  { return { x * o.x, y * o.y, z * o.z }; }
};

inline float dot (V3 a, V3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 normalise (V3 v) noexcept
{
    const float l = std::sqrt (dot (v, v));
    return l > 1.0e-9f ? v * (1.0f / l) : V3 { 0, 0, 1 };
}
inline V3 rgb (juce::Colour c) noexcept { return { c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue() }; }

// Value noise / fBm in [0, 1].
float hash (int x, int y, int seed) noexcept;
float noise (float x, float y, int seed) noexcept;
float fbm (float x, float y, int octaves, int seed) noexcept;

// Studio lighting shared by everything, so all parts agree on where the light is.
struct Lights
{
    static V3 key() noexcept;        // warm key light from the upper left
    static V3 fill() noexcept;       // weak cool fill from the lower right
    static V3 environment (V3 reflected) noexcept; // what chrome "sees"
};

enum class Finish
{
    metallicFlake,   // automotive-style metal flake under a clear coat
    hammertone,      // hammered enamel
    gloss            // smooth gloss enamel
};

struct EnclosureLook
{
    juce::Colour paint;
    Finish finish = Finish::metallicFlake;
    int seed = 1;
};

// Die-cast pedal enclosure seen from above: rounded, bevelled edges, painted.
juce::Image enclosure (float widthPt, float heightPt, float scale, const EnclosureLook& look);

// Davies-style knob: fluted black skirt with a spun-aluminium cap (no pointer).
juce::Image knob (float diameterPt, float scale);

// Stomp switch: steel hex nut, washer and a knurled chrome cap.
juce::Image footswitch (float sizePt, float scale, bool pressed);

// Chrome LED bezel with a red lens, lit or unlit.
juce::Image led (float sizePt, float scale, bool lit);

// Studio floor (walnut planks under a warm spotlight) with a pedalboard on it.
juce::Image board (float widthPt, float heightPt, float scale, juce::Rectangle<float> boardArea);

// Brushed, anodised dark aluminium (header plate).
juce::Image brushedPlate (float widthPt, float heightPt, float scale);

} // namespace orion::render
