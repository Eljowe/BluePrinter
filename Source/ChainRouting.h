#pragma once

#include <JuceHeader.h>

// Pure audio-buffer steps of the parallel chain routing in processBlock:
// building each chain's input scratch from its selected channels, and
// summing a chain's output back into the monitor mix / record bus. Kept free
// of processor state so it can be unit-tested without a plugin host
// (Tests/test_ChainRouting.cpp).
namespace ChainRouting
{
    // Clears dest, then copies the selected channels the layout actually
    // has (mask bit n = channel n, ch < numChannels) **compacted** onto
    // dest's first channels, in ascending order. Compacting (rather than
    // preserving positions) lets any input feed a chain: a mono/stereo
    // plugin only reads its first channels, so a mic on input 4 would be
    // invisible if it were left on scratch channel 4. Channels the chain
    // didn't select — or that the layout doesn't have — stay silent.
    inline void copyInputChannels (juce::AudioBuffer<float>& dest,
                                   const juce::AudioBuffer<float>& source,
                                   int mask,
                                   int numChannels,
                                   int numSamples)
    {
        dest.clear();
        const int destChannels   = dest.getNumChannels();
        const int sourceChannels = juce::jmin (numChannels, source.getNumChannels());
        int out = 0;
        for (int ch = 0; ch < sourceChannels && out < destChannels; ++ch)
            if ((mask & (1 << ch)) != 0)
                dest.copyFrom (out++, 0, source, ch, 0, numSamples);
    }

    // Adds source into dest (scaled by gain) across the channels the two
    // buffers share and that actually exist in this block (ch < numChannels).
    // Used for the monitor mix and, for record-enabled chains, the capture
    // bus.
    inline void sumInto (juce::AudioBuffer<float>& dest,
                         const juce::AudioBuffer<float>& source,
                         float gain,
                         int numSamples,
                         int numChannels)
    {
        const int channels = juce::jmin (
            juce::jmin (dest.getNumChannels(), source.getNumChannels()), numChannels);
        for (int ch = 0; ch < channels; ++ch)
            dest.addFrom (ch, 0, source, ch, 0, numSamples, gain);
    }
}
