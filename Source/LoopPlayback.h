#pragma once

#include <JuceHeader.h>
#include <cstdint>

// Pure audio-buffer render steps shared by the looper and the take
// recorder's overdub monitor:
//
//   - render()  : additive playback of a loop window with a seam de-click,
//                 phase wrap and one-shot stop.
//   - mixLayer(): the pedal-style wrap-mix of a captured layer into a loop.
//
// Kept free of processor state (the caller passes the buffers and gains) so
// the DSP has one definition and is unit-tested without an audio device
// (Tests/test_LoopPlayback.cpp).
namespace LoopPlayback
{
    // Adds source[start + position] into dest, per sample, for dest's whole
    // block. Returns the new play position. When `looping` is false the
    // render stops at the end of the window (the returned position is then
    // >= length and the caller stops playback); otherwise the phase wraps to
    // 0 so every cycle starts exactly at `start`. `declickSamples` is a
    // short fade in/out at the cycle seam; `gain` scales the loop. When
    // `outBlockPeak` is non-null it receives the peak of the value added
    // this block (post gain/de-click), for the playback meter.
    inline int64_t render (juce::AudioBuffer<float>& dest,
                           const juce::AudioBuffer<float>& source,
                           int64_t start,
                           int64_t length,
                           int64_t position,
                           bool looping,
                           float gain,
                           int declickSamples,
                           float* outBlockPeak = nullptr)
    {
        const int numSamples = dest.getNumSamples();
        if (length <= 0 || start < 0 || numSamples <= 0
            || start + length > source.getNumSamples())
            return position;

        const int channels = juce::jmin (dest.getNumChannels(), source.getNumChannels());
        float blockPeak = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            if (position >= length)
            {
                if (! looping)
                    break;
                position = 0;
            }

            float env = 1.0f;
            if (declickSamples > 0)
            {
                const float fadeIn = static_cast<float> (position + 1)
                                   / static_cast<float> (declickSamples + 1);
                const float fadeOut = static_cast<float> (length - position)
                                    / static_cast<float> (declickSamples + 1);
                env = juce::jmin (1.0f, juce::jmin (fadeIn, fadeOut));
            }

            const int src = static_cast<int> (start + position);
            for (int ch = 0; ch < channels; ++ch)
            {
                const float v = source.getSample (ch, src) * gain * env;
                blockPeak = juce::jmax (blockPeak, std::abs (v));
                dest.addSample (ch, i, v);
            }

            ++position;
        }

        if (outBlockPeak != nullptr)
            *outBlockPeak = blockPeak;

        return position;
    }

    // Sums the layer region [layerBase, layerBase + layerLength) into the
    // loop window [loopStart, loopStart + loopLength), wrapping across loop
    // cycles pedal style — audio past the loop end lands on the next cycle.
    // `gain` scales every layer sample before summing.
    inline void mixLayer (juce::AudioBuffer<float>& buffer,
                          int64_t loopStart,
                          int64_t loopLength,
                          int64_t layerBase,
                          int64_t layerLength,
                          float gain)
    {
        if (loopLength <= 0 || layerLength <= 0 || loopStart < 0 || layerBase < 0
            || loopStart + loopLength > buffer.getNumSamples()
            || layerBase + layerLength > buffer.getNumSamples())
            return;

        const int channels = buffer.getNumChannels();
        for (int64_t offset = 0; offset < layerLength;)
        {
            const auto cyclePos = offset % loopLength;
            const auto toMix = juce::jmin (loopLength - cyclePos, layerLength - offset);
            for (int ch = 0; ch < channels; ++ch)
                buffer.addFrom (ch, static_cast<int> (loopStart + cyclePos),
                                buffer, ch, static_cast<int> (layerBase + offset),
                                static_cast<int> (toMix), gain);
            offset += toMix;
        }
    }
}
