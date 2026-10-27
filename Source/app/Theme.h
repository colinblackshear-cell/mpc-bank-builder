#pragma once

#include <juce_graphics/juce_graphics.h>

// Colours loosely after the MPC2000XL: dark grey body, light grey pads, a warm orange accent.
namespace Theme
{
    inline const juce::Colour background { 0xff202124 };
    inline const juce::Colour panel { 0xff2b2c30 };
    inline const juce::Colour padEmpty { 0xff3a3b40 };
    inline const juce::Colour padFull { 0xff55575e };
    inline const juce::Colour waveform { 0xffb8bcc6 };
    inline const juce::Colour accent { 0xffff8a3d };
    inline const juce::Colour text { 0xffeeeeee };
    inline const juce::Colour textDim { 0xff9a9ca3 };
    inline const juce::Colour warning { 0xffff5a52 };
}
