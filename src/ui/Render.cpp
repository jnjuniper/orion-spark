#include "Render.h"

namespace orion::render
{

namespace
{
    float clamp01 (float v) noexcept { return juce::jlimit (0.0f, 1.0f, v); }
    float sq (float v) noexcept { return v * v; }
    V3 lerp (V3 a, V3 b, float t) noexcept { return a + (b - a) * t; }
    V3 grey (float v) noexcept { return { v, v, v }; }
    V3 reflectView (V3 n) noexcept { return n * (2.0f * n.z) - V3 { 0, 0, 1 }; } // view = +z

    // Soft shoulder so highlights roll off instead of clipping.
    float toneMapChannel (float x) noexcept
    {
        x = juce::jmax (0.0f, x);
        return x < 0.8f ? x : 0.8f + 0.2f * (1.0f - std::exp (-(x - 0.8f) * 5.0f));
    }

    struct Canvas
    {
        juce::Image image;
        juce::Image::BitmapData data;

        Canvas (int w, int h)
            : image (juce::Image::ARGB, juce::jmax (1, w), juce::jmax (1, h), true),
              data (image, juce::Image::BitmapData::writeOnly)
        {
        }

        int width() const noexcept  { return image.getWidth(); }
        int height() const noexcept { return image.getHeight(); }

        void put (int x, int y, V3 c, float alpha) noexcept
        {
            const float a = clamp01 (alpha);
            if (a <= 0.0f)
                return;

            auto* p = reinterpret_cast<juce::PixelARGB*> (data.getPixelPointer (x, y));
            const auto ch = [a] (float v) { return (juce::uint8) juce::roundToInt (clamp01 (toneMapChannel (v)) * a * 255.0f); };
            p->setARGB ((juce::uint8) juce::roundToInt (a * 255.0f), ch (c.x), ch (c.y), ch (c.z));
        }
    };

    // Signed distance to a rounded rectangle centred on the origin (negative inside).
    float roundedRectSdf (float px, float py, float halfW, float halfH, float radius) noexcept
    {
        const float qx = std::abs (px) - (halfW - radius);
        const float qy = std::abs (py) - (halfH - radius);
        const float outside = std::sqrt (sq (juce::jmax (qx, 0.0f)) + sq (juce::jmax (qy, 0.0f)));
        return outside + juce::jmin (juce::jmax (qx, qy), 0.0f) - radius;
    }

    // Regular hexagon with flat top/bottom; 'apothem' is the centre-to-edge distance.
    float hexagonSdf (float px, float py, float apothem) noexcept
    {
        constexpr float kx = -0.866025404f, ky = 0.5f, kz = 0.577350269f;
        px = std::abs (px);
        py = std::abs (py);
        const float d = 2.0f * juce::jmin (kx * px + ky * py, 0.0f);
        px -= d * kx;
        py -= d * ky;
        px -= juce::jlimit (-kz * apothem, kz * apothem, px);
        py -= apothem;
        return std::sqrt (px * px + py * py) * (py < 0.0f ? -1.0f : 1.0f);
    }

    // Quarter-round bevel slope for a point 't' of the way in from the edge (0 = edge).
    float bevelSlope (float t) noexcept
    {
        if (t >= 1.0f)
            return 0.0f;
        const float a = 1.0f - t;
        return juce::jmin (3.0f, a / std::sqrt (juce::jmax (1.0e-3f, 1.0f - a * a)));
    }

    // Painted / plastic surface: ambient + two diffuse lights + Blinn-Phong + clear coat.
    V3 shade (V3 n, V3 base, float specStrength, float shininess, float clearCoat) noexcept
    {
        const V3 view { 0, 0, 1 };
        const V3 key = Lights::key(), fill = Lights::fill();

        const float diffuse = juce::jmax (0.0f, dot (n, key)) * 0.95f + juce::jmax (0.0f, dot (n, fill)) * 0.22f;
        V3 c = base * V3 { 1.04f, 1.0f, 0.95f } * (0.16f + diffuse * 0.82f);

        const V3 h = normalise (key + view);
        c = c + V3 { 1.0f, 0.94f, 0.86f } * (std::pow (juce::jmax (0.0f, dot (n, h)), shininess) * specStrength);

        const float fresnel = 0.04f + 0.96f * std::pow (1.0f - juce::jmax (0.0f, dot (n, view)), 5.0f);
        return c + Lights::environment (reflectView (n)) * (clearCoat * fresnel);
    }

    V3 chrome (V3 n, float tint = 1.0f) noexcept
    {
        return Lights::environment (reflectView (n)) * (0.92f * tint) + grey (0.03f);
    }
}

// ---------------------------------------------------------------------------
float hash (int x, int y, int seed) noexcept
{
    auto h = (juce::uint32) x * 374761393u + (juce::uint32) y * 668265263u + (juce::uint32) seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float) (h & 0xffffffu) / (float) 0xffffffu;
}

float noise (float x, float y, int seed) noexcept
{
    const float fx = std::floor (x), fy = std::floor (y);
    const int xi = (int) fx, yi = (int) fy;
    const float tx = x - fx, ty = y - fy;
    const float u = tx * tx * (3.0f - 2.0f * tx), v = ty * ty * (3.0f - 2.0f * ty);

    const float a = hash (xi, yi, seed), b = hash (xi + 1, yi, seed);
    const float c = hash (xi, yi + 1, seed), d = hash (xi + 1, yi + 1, seed);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}

float fbm (float x, float y, int octaves, int seed) noexcept
{
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i)
    {
        sum += amp * noise (x, y, seed + i * 101);
        norm += amp;
        x *= 2.03f;
        y *= 2.03f;
        amp *= 0.5f;
    }
    return sum / norm;
}

V3 Lights::key() noexcept  { return normalise ({ -0.45f, -0.75f, 0.85f }); }
V3 Lights::fill() noexcept { return normalise ({ 0.6f, 0.55f, 0.5f }); }

V3 Lights::environment (V3 r) noexcept
{
    // A dim studio: dark ceiling, a bright window/softbox up-left, warm wood below.
    const float up = juce::jlimit (-1.0f, 1.0f, r.z);
    V3 c = lerp ({ 0.15f, 0.135f, 0.125f }, { 0.06f, 0.06f, 0.07f }, clamp01 (up));

    // Overhead studio lights: what chrome facing straight up reflects.
    c = c + V3 { 0.62f, 0.58f, 0.52f } * (std::pow (clamp01 (up), 5.0f) * 0.55f);

    const float horizon = 1.0f - clamp01 (std::abs (up));
    c = c + V3 { 0.45f, 0.49f, 0.55f } * (horizon * clamp01 (-r.y) * 0.9f)
          + V3 { 0.42f, 0.25f, 0.12f } * (horizon * clamp01 (r.y) * 0.8f);

    const float s = juce::jmax (0.0f, dot (r, key()));
    c = c + V3 { 1.0f, 0.93f, 0.82f } * (std::pow (s, 70.0f) * 3.2f + std::pow (s, 9.0f) * 0.32f);

    const float f = juce::jmax (0.0f, dot (r, fill()));
    return c + V3 { 0.55f, 0.65f, 0.85f } * (std::pow (f, 40.0f) * 0.55f);
}

// ---------------------------------------------------------------------------
juce::Image enclosure (float widthPt, float heightPt, float scale, const EnclosureLook& look)
{
    Canvas canvas (juce::roundToInt (widthPt * scale), juce::roundToInt (heightPt * scale));
    const float halfW = (float) canvas.width() * 0.5f - 0.5f, halfH = (float) canvas.height() * 0.5f - 0.5f;
    const float radius = 10.0f * scale, bevel = 9.0f * scale;
    const V3 paint = rgb (look.paint);
    const V3 view { 0, 0, 1 };
    const V3 halfVec = normalise (Lights::key() + view);

    float specStrength = 0.35f, shininess = 45.0f, clearCoat = 0.9f;
    if (look.finish == Finish::hammertone) { specStrength = 0.22f; shininess = 22.0f; clearCoat = 0.35f; }
    if (look.finish == Finish::gloss)      { specStrength = 0.55f; shininess = 90.0f; clearCoat = 1.0f; }

    const auto sdf = [&] (float px, float py) { return roundedRectSdf (px, py, halfW, halfH, radius); };

    for (int y = 0; y < canvas.height(); ++y)
        for (int x = 0; x < canvas.width(); ++x)
        {
            const float px = (float) x + 0.5f - (float) canvas.width() * 0.5f;
            const float py = (float) y + 0.5f - (float) canvas.height() * 0.5f;
            const float d = sdf (px, py);
            if (d > 1.0f)
                continue;

            const float gx = sdf (px + 1.0f, py) - sdf (px - 1.0f, py);
            const float gy = sdf (px, py + 1.0f) - sdf (px, py - 1.0f);
            const float gl = juce::jmax (1.0e-6f, std::sqrt (gx * gx + gy * gy));
            const float slope = bevelSlope (clamp01 (-d / bevel)) * 0.85f;
            V3 n { gx / gl * slope, gy / gl * slope, 1.0f };

            const float u = (float) x / scale, v = (float) y / scale;   // point space: same look at any scale
            V3 base = paint * (1.07f - 0.14f * ((float) y / (float) canvas.height()));
            float sparkle = 0.0f;

            // Orange peel: very gentle large-scale waviness of the paint.
            const float op = fbm (u * 0.35f, v * 0.35f, 2, look.seed + 7);
            n.x += (fbm ((u + 0.6f) * 0.35f, v * 0.35f, 2, look.seed + 7) - op) * 0.25f;
            n.y += (fbm (u * 0.35f, (v + 0.6f) * 0.35f, 2, look.seed + 7) - op) * 0.25f;

            if (look.finish == Finish::metallicFlake)
            {
                const int cx = (int) std::floor (u * 3.4f), cy = (int) std::floor (v * 3.4f);
                const float h1 = hash (cx, cy, look.seed), h2 = hash (cx, cy, look.seed + 1), h3 = hash (cx, cy, look.seed + 2);
                const V3 flake = normalise ({ (h1 - 0.5f) * 0.9f, (h2 - 0.5f) * 0.9f, 1.0f });
                sparkle = std::pow (juce::jmax (0.0f, dot (flake, halfVec)), 180.0f) * (h3 > 0.72f ? 0.55f : 0.0f);
                base = base * (0.95f + 0.1f * h3);
            }
            else if (look.finish == Finish::hammertone)
            {
                const float f = 0.24f;
                const float h0 = fbm (u * f, v * f, 3, look.seed);
                const float hx = fbm ((u + 0.8f) * f, v * f, 3, look.seed);
                const float hy = fbm (u * f, (v + 0.8f) * f, 3, look.seed);
                n.x += (h0 - hx) * 3.2f;
                n.y += (h0 - hy) * 3.2f;
                base = base * (0.88f + 0.24f * h0);
            }

            n = normalise (n);
            V3 c = shade (n, base, specStrength, shininess, clearCoat) + lerp (paint * 2.2f, V3 { 1.0f, 0.95f, 0.88f }, 0.45f) * sparkle;

            // A hint of wear/darkening right at the rim.
            c = c * (0.82f + 0.18f * clamp01 (-d / (2.0f * scale)));
            canvas.put (x, y, c, 0.5f - d);
        }

    return canvas.image;
}

// ---------------------------------------------------------------------------
juce::Image knob (float diameterPt, float scale)
{
    Canvas canvas (juce::roundToInt (diameterPt * scale), juce::roundToInt (diameterPt * scale));
    const float centre = (float) canvas.width() * 0.5f;
    const float radius = centre - 1.0f * scale;
    const V3 key = Lights::key();
    const float lightAngle = std::atan2 (key.y, key.x);
    constexpr int flutes = 26;

    for (int y = 0; y < canvas.height(); ++y)
        for (int x = 0; x < canvas.width(); ++x)
        {
            const float dx = ((float) x + 0.5f - centre) / radius;
            const float dy = ((float) y + 0.5f - centre) / radius;
            const float rr = std::sqrt (dx * dx + dy * dy);
            const float theta = std::atan2 (dy, dx);
            const float edge = 1.0f - 0.035f * (0.5f + 0.5f * std::cos ((float) flutes * theta));
            const float alpha = (edge - rr) * radius + 0.5f;
            if (alpha <= 0.0f)
                continue;

            const V3 radial = rr > 1.0e-4f ? V3 { dx / rr, dy / rr, 0 } : V3 { 0, 0, 0 };
            const V3 tangent { -radial.y, radial.x, 0 };
            V3 c;

            if (rr > 0.80f)
            {
                // Rounded, fluted outer edge of the skirt.
                const float t = clamp01 ((rr - 0.80f) / juce::jmax (0.01f, edge - 0.80f));
                const float tilt = std::pow (t, 1.6f) * 2.2f;
                const float flute = -std::sin ((float) flutes * theta) * 0.55f * clamp01 (t * 1.5f);
                const V3 n = normalise (radial * tilt + tangent * flute + V3 { 0, 0, 1 });
                c = shade (n, grey (0.035f), 0.45f, 28.0f, 0.5f);
            }
            else if (rr > 0.60f)
            {
                // Flat top of the skirt (where the pointer line sits).
                const float grain = noise ((float) x / scale * 2.0f, (float) y / scale * 2.0f, 5);
                const V3 n = normalise (radial * 0.10f + V3 { 0, 0, 1 });
                c = shade (n, grey (0.048f + 0.01f * grain), 0.3f, 22.0f, 0.35f);
            }
            else if (rr > 0.555f)
            {
                // Groove around the cap.
                const V3 n = normalise (radial * -1.2f + V3 { 0, 0, 1 });
                c = shade (n, grey (0.02f), 0.2f, 20.0f, 0.2f) * 0.6f;
            }
            else
            {
                // Spun aluminium cap: domed, concentric machining rings, bow-tie highlight.
                const float cr = rr / 0.555f;
                const V3 n = cr > 0.93f ? normalise (radial * 1.4f + V3 { 0, 0, 1 })
                                        : normalise ({ dx / 0.555f * 0.25f, dy / 0.555f * 0.25f, 1.0f });
                const float aniso = std::pow (std::abs (std::cos (theta - lightAngle)), 12.0f);
                const float rings = 0.90f + 0.20f * noise (rr * radius / scale * 2.4f, 0.5f, 77);
                const float diffuse = juce::jmax (0.0f, dot (n, key));
                c = V3 { 0.60f, 0.62f, 0.65f } * ((0.30f + 0.55f * diffuse) * rings)
                  + V3 { 1.0f, 0.95f, 0.9f } * (aniso * 0.55f * rings)
                  + Lights::environment (reflectView (n)) * 0.30f;
                if (cr > 0.93f)
                    c = c * 0.75f;
            }

            canvas.put (x, y, c, alpha);
        }

    return canvas.image;
}

// ---------------------------------------------------------------------------
juce::Image footswitch (float sizePt, float scale, bool pressed)
{
    Canvas canvas (juce::roundToInt (sizePt * scale), juce::roundToInt (sizePt * scale));
    const float centre = (float) canvas.width() * 0.5f;
    const float radius = centre - 1.0f * scale;
    const float capRadius = 0.56f * (pressed ? 0.965f : 1.0f);
    constexpr float apothem = 0.86f;

    for (int y = 0; y < canvas.height(); ++y)
        for (int x = 0; x < canvas.width(); ++x)
        {
            const float X = ((float) x + 0.5f - centre) / radius;
            const float Y = ((float) y + 0.5f - centre) / radius;
            const float dh = hexagonSdf (X, Y, apothem);
            const float alpha = -dh * radius + 0.5f;
            if (alpha <= 0.0f)
                continue;

            const float rr = std::sqrt (X * X + Y * Y);
            const V3 radial = rr > 1.0e-4f ? V3 { X / rr, Y / rr, 0 } : V3 { 0, 0, 0 };
            V3 c;

            if (rr < capRadius)
            {
                const float cn = rr / capRadius;
                V3 n { X / capRadius * 0.55f, Y / capRadius * 0.55f, 1.0f };
                if (cn < 0.86f)
                    n = n + radial * (0.22f * std::sin (cn * 44.0f));      // knurled rings
                if (cn > 0.9f)
                    n = n + radial * 1.5f;                                  // edge chamfer
                c = chrome (normalise (n), pressed ? 0.82f : 1.0f);
            }
            else if (rr < 0.70f)
            {
                // Washer.
                const float t = (rr - capRadius) / (0.70f - capRadius);
                const V3 n = normalise (radial * (t > 0.8f ? 0.9f : 0.05f) + V3 { 0, 0, 1 });
                c = shade (n, grey (0.38f), 0.6f, 60.0f, 0.2f) + chrome (n) * 0.25f;
                if (t < 0.12f)
                    c = c * 0.35f;                                          // shadow line under the cap
            }
            else
            {
                // Hex nut: flat top with chamfered edges.
                const float gx = hexagonSdf (X + 0.01f, Y, apothem) - hexagonSdf (X - 0.01f, Y, apothem);
                const float gy = hexagonSdf (X, Y + 0.01f, apothem) - hexagonSdf (X, Y - 0.01f, apothem);
                const float gl = juce::jmax (1.0e-6f, std::sqrt (gx * gx + gy * gy));
                const float chamfer = dh > -0.08f ? 1.1f : 0.0f;
                const V3 n = normalise ({ gx / gl * chamfer, gy / gl * chamfer, 1.0f });
                c = shade (n, grey (0.34f), 0.7f, 50.0f, 0.15f) + chrome (n) * 0.2f;
            }

            canvas.put (x, y, c, alpha);
        }

    return canvas.image;
}

// ---------------------------------------------------------------------------
juce::Image led (float sizePt, float scale, bool lit)
{
    Canvas canvas (juce::roundToInt (sizePt * scale), juce::roundToInt (sizePt * scale));
    const float centre = (float) canvas.width() * 0.5f;
    const float radius = centre - 0.5f * scale;

    for (int y = 0; y < canvas.height(); ++y)
        for (int x = 0; x < canvas.width(); ++x)
        {
            const float X = ((float) x + 0.5f - centre) / radius;
            const float Y = ((float) y + 0.5f - centre) / radius;
            const float rr = std::sqrt (X * X + Y * Y);
            const float alpha = (1.0f - rr) * radius + 0.5f;
            if (alpha <= 0.0f)
                continue;

            const V3 radial = rr > 1.0e-4f ? V3 { X / rr, Y / rr, 0 } : V3 { 0, 0, 0 };
            V3 c;

            if (rr > 0.6f)
            {
                const float t = (rr - 0.6f) / 0.4f * 2.0f - 1.0f;          // across the bezel ring
                c = chrome (normalise (radial * (t * 1.6f) + V3 { 0, 0, 1 }));
            }
            else
            {
                const float ln = rr / 0.6f;
                const V3 n = normalise ({ X / 0.6f * 0.7f, Y / 0.6f * 0.7f, 1.0f });
                if (lit)
                {
                    const float core = std::exp (-ln * ln * 2.5f);
                    c = V3 { 1.0f, 0.22f, 0.10f } * (0.85f + 0.6f * core) + V3 { 1.0f, 0.85f, 0.65f } * (core * core * 0.9f);
                    c = c + Lights::environment (reflectView (n)) * 0.25f;
                }
                else
                {
                    c = shade (n, { 0.22f, 0.03f, 0.025f }, 0.9f, 140.0f, 0.7f);
                }
                if (ln > 0.88f)
                    c = c * 0.55f;
            }

            canvas.put (x, y, c, alpha);
        }

    return canvas.image;
}

// ---------------------------------------------------------------------------
juce::Image board (float widthPt, float heightPt, float scale, juce::Rectangle<float> area)
{
    Canvas canvas (juce::roundToInt (widthPt * scale), juce::roundToInt (heightPt * scale));
    const auto c0 = area.getCentre();
    const float halfW = area.getWidth() * 0.5f, halfH = area.getHeight() * 0.5f;
    const float frameWidth = 9.0f;
    const float slatHeight = (area.getHeight() - 2.0f * frameWidth) / 3.0f;
    constexpr float plankHeight = 38.0f;

    const auto boardSdf = [&] (float u, float v) { return roundedRectSdf (u - c0.x, v - c0.y, halfW, halfH, 10.0f); };

    for (int y = 0; y < canvas.height(); ++y)
        for (int x = 0; x < canvas.width(); ++x)
        {
            const float u = ((float) x + 0.5f) / scale, v = ((float) y + 0.5f) / scale;

            // Warm spotlight from above, slightly left of centre.
            const float lx = (u - widthPt * 0.42f) / (widthPt * 0.55f);
            const float ly = (v - heightPt * 0.32f) / (heightPt * 0.8f);
            const float spot = 0.22f + 0.95f * std::exp (-(lx * lx + ly * ly) * 1.7f);

            // --- Walnut floor.
            const int plank = (int) std::floor (v / plankHeight);
            const float shift = hash (plank, 0, 11) * 900.0f;
            const float wu = u + shift;
            const float warp = fbm (wu * 0.004f, v * 0.05f, 3, 21) * 30.0f;
            const float grain = 0.5f + 0.5f * std::sin ((v + warp) * 0.55f + fbm (wu * 0.01f, v * 0.2f, 2, 5) * 6.0f);
            const float fine = fbm (wu * 0.08f, v * 1.3f, 2, 9);
            const float tone = 0.82f + 0.32f * hash (plank, 1, 13);
            V3 wood = lerp ({ 0.11f, 0.055f, 0.03f }, { 0.30f, 0.16f, 0.08f }, clamp01 (grain * 0.55f + fine * 0.45f)) * tone;

            const float inPlank = std::fmod (v, plankHeight);
            if (inPlank < 1.0f)
                wood = wood * 0.3f;
            else if (inPlank < 2.0f)
                wood = wood * 1.15f;
            if (std::abs (std::fmod (wu, 780.0f)) < 1.0f)
                wood = wood * 0.35f;

            wood = wood * V3 { 1.06f, 0.95f, 0.84f } * spot;
            wood = wood + V3 { 1.0f, 0.85f, 0.65f } * (std::pow (clamp01 (spot - 0.55f), 2.0f) * 0.12f * (0.5f + fine)); // satin sheen

            // Soft shadow of the board on the floor (light from the upper left).
            const float ds = boardSdf (u - 7.0f, v - 12.0f);
            wood = wood * (1.0f - 0.72f * std::exp (-juce::jmax (0.0f, ds) / 13.0f));

            // --- Pedalboard.
            const float db = boardSdf (u, v);
            V3 c = wood;

            if (db < 1.0f / scale)
            {
                V3 b;
                if (-db < frameWidth)
                {
                    // Anodised aluminium frame with a rounded top edge.
                    const float gx = boardSdf (u + 0.5f, v) - boardSdf (u - 0.5f, v);
                    const float gy = boardSdf (u, v + 0.5f) - boardSdf (u, v - 0.5f);
                    const float gl = juce::jmax (1.0e-6f, std::sqrt (gx * gx + gy * gy));
                    const float slope = bevelSlope (clamp01 (-db / 5.0f));
                    const float innerLip = (-db > frameWidth - 1.5f) ? -1.2f : 0.0f;    // step down to the surface
                    const V3 n = normalise ({ gx / gl * (slope + innerLip), gy / gl * (slope + innerLip), 1.0f });
                    b = shade (n, grey (0.07f), 0.55f, 40.0f, 0.5f) * (0.55f + 0.45f * spot);
                }
                else
                {
                    // Black loop-velcro surface, three slats with grooves between them.
                    const float fibre = hash ((int) (u * 3.0f), (int) (v * 3.0f), 3) * 0.022f + fbm (u * 0.5f, v * 0.5f, 2, 4) * 0.02f;
                    const float inSlat = std::fmod (v - (area.getY() + frameWidth), slatHeight);
                    float groove = 1.0f;
                    if (inSlat < 2.0f) groove = 0.25f;
                    else if (inSlat < 3.0f) groove = 1.6f;
                    b = grey (0.040f + fibre) * (groove * (0.45f + 0.75f * spot));
                    b = b * (1.0f - 0.35f * std::exp (-(-db - frameWidth) / 4.0f));      // occlusion under the frame lip
                }

                c = lerp (wood, b, clamp01 (0.5f - db * scale));
            }

            canvas.put (x, y, c, 1.0f);
        }

    return canvas.image;
}

// ---------------------------------------------------------------------------
juce::Image brushedPlate (float widthPt, float heightPt, float scale)
{
    Canvas canvas (juce::roundToInt (widthPt * scale), juce::roundToInt (heightPt * scale));

    for (int y = 0; y < canvas.height(); ++y)
        for (int x = 0; x < canvas.width(); ++x)
        {
            const float u = ((float) x + 0.5f) / scale, v = ((float) y + 0.5f) / scale;
            const float streak = fbm (u * 0.015f, v * 2.4f, 3, 31);
            const float speck = hash (x, y, 7) * 0.012f;
            const float band = std::exp (-sq ((u - widthPt * 0.3f) / (widthPt * 0.2f))) * 0.07f * (0.6f + 0.8f * streak);

            V3 c = grey (0.075f + 0.05f * streak + speck + band) * V3 { 0.98f, 1.0f, 1.04f };
            c = c * (1.06f - 0.16f * (v / heightPt));

            if (v < 1.5f)                 c = c + grey (0.14f);    // top bevel catches the light
            if (v > heightPt - 1.5f)      c = c * 0.35f;           // bottom edge in shadow

            canvas.put (x, y, c, 1.0f);
        }

    return canvas.image;
}

} // namespace orion::render
