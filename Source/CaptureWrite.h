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
    // not exceed dest's sample count. `sourceOffset` skips that many frames
    // from the start of `source` (the capture cursor offset of 0046);
    // `numSamples` counts the frames to copy AFTER the offset, and the copy
    // is clamped to what the source actually has left.
    inline int64_t write (juce::AudioBuffer<float>& dest,
                          int64_t start,
                          const juce::AudioBuffer<float>& source,
                          int numSamples,
                          int64_t limit,
                          int sourceOffset = 0)
    {
        if (start < 0 || start >= limit || limit > dest.getNumSamples())
            return start;

        const int offset = juce::jmax (0, sourceOffset);
        const int available = juce::jmax (0, source.getNumSamples() - offset);
        const int requested = juce::jmin (numSamples, available);
        if (requested <= 0)
            return start;

        const int count = static_cast<int> (juce::jmin<int64_t> (requested, limit - start));
        if (count <= 0)
            return start;

        const int channels = juce::jmin (dest.getNumChannels(), source.getNumChannels());
        for (int ch = 0; ch < channels; ++ch)
            dest.copyFrom (ch, static_cast<int> (start), source, ch, offset, count);

        return start + count;
    }
}
