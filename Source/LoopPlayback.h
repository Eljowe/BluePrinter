#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <cstdint>

// Pure audio-buffer render steps shared by the looper and the take
// recorder's overdub monitor:
//
//   - render()    : forward 1x additive playback (take-overdub monitor).
//   - renderMode(): additive playback with reverse / half-speed, seam
//                   de-click, phase wrap and one-shot stop.
//   - mixLayer(): the pedal-style wrap-mix of a captured layer into a loop.
//
// Kept free of processor state (the caller passes the buffers and gains) so
// the DSP has one definition and is unit-tested without an audio device
// (Tests/test_LoopPlayback.cpp).
namespace LoopPlayback
{
    // Direction and speed for window playback. `rate` is the number of source
    // samples consumed per output sample (0.5 = tape-style half-speed).
    struct PlaybackMode
    {
        bool   reverse = false;
        double rate    = 1.0;
    };

    // Generalised window playback shared by the looper and the take-overdub
    // monitor. `position` is a phase in source samples (possibly fractional)
    // that always advances forward by mode.rate per output sample and wraps to
    // 0 when `looping`. mode.reverse only maps the phase to the source read:
    // forward reads source[start + position], reverse reads the window mirrored
    // as source[start + length - 1 - position], so the audible seam and the
    // one-shot-stop position are unchanged. Reads use linear interpolation,
    // crossing the loop seam (last <-> first sample) while looping and holding
    // at the window edge when not. `declickSamples` is a short fade in/out at
    // the cycle seam; `gain` scales the loop. Returns the new phase (>= length
    // when a non-looping play has ended). When `outBlockPeak` is non-null it
    // receives the peak of the value added this block (post gain/de-click), for
    // the playback meter.
    inline double renderMode (juce::AudioBuffer<float>& dest,
                              const juce::AudioBuffer<float>& source,
                              int64_t start,
                              int64_t length,
                              double position,
                              bool looping,
                              float gain,
                              int declickSamples,
                              PlaybackMode mode,
                              float* outBlockPeak = nullptr)
    {
        const int numSamples = dest.getNumSamples();
        if (length <= 0 || start < 0 || numSamples <= 0
            || start + length > source.getNumSamples()
            || mode.rate <= 0.0)
            return position;

        const int channels = juce::jmin (dest.getNumChannels(), source.getNumChannels());
        const bool reverse = mode.reverse;
        const int windowLength = static_cast<int> (length);
        const int lastIndex = windowLength - 1;
        float blockPeak = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            if (position >= static_cast<double> (length))
            {
                if (! looping)
                    break;
                position = 0.0;
            }

            float env = 1.0f;
            if (declickSamples > 0)
            {
                const float fadeIn = static_cast<float> (position + 1.0)
                                   / static_cast<float> (declickSamples + 1);
                const float fadeOut = static_cast<float> (static_cast<double> (length) - position)
                                    / static_cast<float> (declickSamples + 1);
                env = juce::jmin (1.0f, juce::jmin (fadeIn, fadeOut));
            }

            const double readOffset = reverse
                ? static_cast<double> (length - 1) - position
                : position;
            const double floorOffset = std::floor (readOffset);
            float frac = static_cast<float> (readOffset - floorOffset);

            int i0 = static_cast<int> (floorOffset);
            int i1;

            if (looping)
            {
                // Circular window: let the read cross the seam (last -> first).
                // Wrapping the indices also handles reverse's negative offset.
                i1 = i0 + 1;
                i0 = ((i0 % windowLength) + windowLength) % windowLength;
                i1 = ((i1 % windowLength) + windowLength) % windowLength;
            }
            else
            {
                if (i0 < 0 || i0 > lastIndex)
                {
                    i0 = juce::jlimit (0, lastIndex, i0);
                    frac = 0.0f;
                }
                i1 = juce::jmin (i0 + 1, lastIndex);
            }

            const int srcA = static_cast<int> (start) + i0;
            const int srcB = static_cast<int> (start) + i1;

            for (int ch = 0; ch < channels; ++ch)
            {
                const float a = source.getSample (ch, srcA);
                const float b = source.getSample (ch, srcB);
                const float v = (a + (b - a) * frac) * gain * env;
                blockPeak = juce::jmax (blockPeak, std::abs (v));
                dest.addSample (ch, i, v);
            }

            position += mode.rate;
        }

        if (outBlockPeak != nullptr)
            *outBlockPeak = blockPeak;

        return position;
    }

    // Forward 1x playback, kept as the integer-phase entry point for the
    // take-overdub monitor. Returns the new play position (>= length when a
    // non-looping play has ended); delegates to renderMode.
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
        const auto end = renderMode (dest, source, start, length,
                                     static_cast<double> (position),
                                     looping, gain, declickSamples,
                                     PlaybackMode {}, outBlockPeak);
        return static_cast<int64_t> (std::llround (end));
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
