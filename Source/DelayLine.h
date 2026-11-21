#pragma once

#include <JuceHeader.h>

#include <algorithm>
#include <cstddef>
#include <vector>

// A preallocated circular delay line (0046 plugin delay compensation).
//
// prepare() runs on the message thread and owns all allocation; setDelay() and
// process() run on the audio thread and only read/write the preallocated ring —
// no allocation, no locks. Delay 0 (or an unprepared line) is a bit-identical
// no-op, so a build with no latency-reporting plugins behaves exactly as
// before.
//
// Changing the delay clears the line: a latency change is a discontinuity, and
// keeping stale history would smear the old alignment into the new one.
// Each channel has its own ring, so channels never leak into each other.
//
// Verified by Tests/test_DelayLine.cpp.
class DelayLine
{
public:
    // Message thread. Allocates one ring per channel, long enough for
    // `maxDelaySamples` of history (plus the current sample), and resets the
    // line to delay 0.
    void prepare (int numChannels, int maxDelaySamples)
    {
        numChannels = juce::jmax (1, numChannels);
        maxDelay = juce::jmax (0, maxDelaySamples);
        ringSize = maxDelay + 1;

        storage.assign (static_cast<std::size_t> (numChannels)
                          * static_cast<std::size_t> (ringSize),
                        0.0f);
        channels = numChannels;
        writeIndex = 0;
        delay = 0;
    }

    // Audio thread. Clamps to [0, maxDelay] and clears the line only when the
    // delay actually changes.
    void setDelay (int samples)
    {
        const int clamped = juce::jlimit (0, maxDelay, samples);
        if (clamped != delay)
        {
            delay = clamped;
            clearSamples();
        }
    }

    // Audio thread. Delays `buffer` in place by the current delay. Not
    // prepared or delay 0 leaves the buffer untouched.
    void process (juce::AudioBuffer<float>& buffer, int numSamples)
    {
        if (delay <= 0 || ringSize <= 0 || storage.empty())
            return;

        const int numChannels = juce::jmin (channels, buffer.getNumChannels());
        const int count = juce::jmin (numSamples, buffer.getNumSamples());
        if (numChannels <= 0 || count <= 0)
            return;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            auto* ring = storage.data()
                       + static_cast<std::size_t> (ch) * static_cast<std::size_t> (ringSize);

            int write = writeIndex;
            for (int i = 0; i < count; ++i)
            {
                int read = write - delay;
                if (read < 0)
                    read += ringSize;

                const float input = data[i];
                data[i] = ring[read];
                ring[write] = input;

                if (++write == ringSize)
                    write = 0;
            }
        }

        writeIndex = (writeIndex + count) % ringSize;
    }

    // Audio thread / message thread with audio stopped. Zeroes the history but
    // keeps the allocated ring and the current delay.
    void clear() { clearSamples(); }

    // Message thread with audio stopped. Frees the ring.
    void release()
    {
        storage.clear();
        channels = 0;
        ringSize = 0;
        maxDelay = 0;
        writeIndex = 0;
        delay = 0;
    }

    int  getDelay() const { return delay; }
    bool isPrepared() const { return ringSize > 0 && ! storage.empty(); }

private:
    void clearSamples()
    {
        std::fill (storage.begin(), storage.end(), 0.0f);
        writeIndex = 0;
    }

    std::vector<float> storage;
    int channels   = 0;
    int maxDelay   = 0;
    int ringSize   = 0;
    int writeIndex = 0;
    int delay      = 0;
};
