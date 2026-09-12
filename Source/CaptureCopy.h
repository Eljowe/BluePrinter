#pragma once

#include <JuceHeader.h>
#include <cstdint>
#include <memory>

// Copies a region out of the shared record buffer into a fresh buffer — the
// take/loop finalize step (peaks, saved snippet). Callers hold whatever lock
// protects `source`. Pure buffer math (Tests/test_CaptureCopy.cpp).
namespace CaptureCopy
{
    // Copies [start, start + length) from every channel of `source` into a
    // new buffer. Returns nullptr when the region is empty or out of bounds,
    // so the caller can skip the save/peaks pass.
    inline std::shared_ptr<juce::AudioBuffer<float>> copyRegion (
        const juce::AudioBuffer<float>& source, int64_t start, int64_t length)
    {
        if (length <= 0 || start < 0 || start + length > source.getNumSamples())
            return nullptr;

        auto dest = std::make_shared<juce::AudioBuffer<float>> (
            source.getNumChannels(), static_cast<int> (length));
        for (int ch = 0; ch < source.getNumChannels(); ++ch)
            dest->copyFrom (ch, 0, source, ch, static_cast<int> (start),
                            static_cast<int> (length));
        return dest;
    }
}
