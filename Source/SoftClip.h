#pragma once

#include <JuceHeader.h>
#include <cmath>

// Monitor/output soft-clip guard (0048).
//
// A fixed soft-knee saturation applied per channel to the monitor buffer,
// AFTER the master Output gain and the tuner monitor mute and immediately
// BEFORE the OUT meter. The ceiling is the curve's asymptote: the output is
// (near-)unity well below it and can never reach it, so a hot monitor
// saturates instead of hard-clipping the DAC. Zero latency, no lookahead and
// no allocation — the processor converts dB to a gain once per block and the
// guard rewrites the existing monitor block in place. The print
// (recordingMixBuffer) is never touched.
//
// Pure logic (no device/processor state), unit-tested directly
// (Tests/test_SoftClip.cpp).
namespace SoftClip
{
    // 10^(dB/20). Callers clamp the ceiling; this is just the conversion.
    inline float ceilingGain (float ceilingDb) noexcept
    {
        return std::pow (10.0f, ceilingDb / 20.0f);
    }

    // Fixed soft-knee saturation: y = ceiling * tanh(x / ceiling).
    // Monotonic, odd-symmetric, near-unity well below the ceiling and
    // asymptotically bounded by `ceiling` (it never reaches it). A ceiling
    // that is not positive is treated as "no guard": the sample passes
    // through unchanged.
    inline float shape (float x, float ceiling) noexcept
    {
        if (! (ceiling > 0.0f))
            return x;

        return ceiling * std::tanh (x / ceiling);
    }

    // In-place per-channel guard over the first numSamples. Returns true
    // when any pre-clip |sample| exceeded the ceiling (the "engaged" latch,
    // which the processor turns into the OUT clip indicator). No-op and
    // returns false when enabled == false or ceiling <= 0.
    inline bool processBuffer (juce::AudioBuffer<float>& buffer, int numSamples,
                               float ceiling, bool enabled) noexcept
    {
        if (! enabled || ! (ceiling > 0.0f) || numSamples <= 0)
            return false;

        const int samples = juce::jmin (numSamples, buffer.getNumSamples());
        bool engaged = false;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            float* data = buffer.getWritePointer (channel);

            for (int i = 0; i < samples; ++i)
            {
                const float x = data[i];

                if (std::abs (x) > ceiling)
                    engaged = true;

                data[i] = shape (x, ceiling);
            }
        }

        return engaged;
    }
} // namespace SoftClip
