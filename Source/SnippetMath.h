#pragma once

#include <JuceHeader.h>

// Pure snippet playback math, extracted from
// BluePrinterAudioProcessor::normalizeSnippet so it can be unit tested
// without a processor instance.
namespace SnippetMath
{
    // Gain (dB) that brings a linear `peak` (0..1+) to -1 dBFS. Returns
    // 0 for a silent/negative peak (nothing to normalize). The caller
    // still clamps the result to the snippet gain range (-24..+24 dB).
    inline float normalizeGainDb (float peak)
    {
        if (peak <= 0.0f)
            return 0.0f;

        return -1.0f - juce::Decibels::gainToDecibels (peak, -144.0f);
    }
}
