#pragma once

#include <juce_graphics/juce_graphics.h>

namespace orion
{

// The Orion Spark mark: the three stars of Orion's belt, the middle one flaring
// into an amber spark. 'size' is the distance from the centre to the outer stars;
// spikes and glow extend to roughly 1.8x that.
void drawOrionSparkMark (juce::Graphics& g, juce::Point<float> centre, float size);

} // namespace orion
