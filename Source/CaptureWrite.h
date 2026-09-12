#pragma once

#include <JuceHeader.h>
#include <cstdint>

// The capture-buffer write shared by the take recorder and the loop capture
// tap: copy a block into the shared record buffer at `start`, clamped so the
// write never runs past `limit` (the buffer capacity), returning the new
// write position. Pure buffer math (Tests/test_CaptureWrite.cpp).
namespace CaptureWrite
{
    // Copies up to numSamples samples from source into dest starting at
    // `start`, stopping at `limit` (the record-buffer capacity). Returns the
    // new write position (== start when there was no room). `limit` should
    // not exceed dest's sample count.
    inline int64_t write (juce::AudioBuffer<float>& dest,
                          int64_t start,
                          const juce::AudioBuffer<float>& source,
                          int numSamples,
                          int64_t limit)
    {
        if (start < 0 || start >= limit || limit > dest.getNumSamples())
            return start;

        const int count = static_cast<int> (juce::jmin<int64_t> (numSamples, limit - start));
        if (count <= 0)
            return start;

        const int channels = juce::jmin (dest.getNumChannels(), source.getNumChannels());
        for (int ch = 0; ch < channels; ++ch)
            dest.copyFrom (ch, static_cast<int> (start), source, ch, 0, count);

        return start + count;
    }
}
