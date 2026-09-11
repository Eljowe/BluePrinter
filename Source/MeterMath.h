#pragma once

#include <JuceHeader.h>
#include <cmath>

// Metering math shared by the input/record/output/loop meters and the
// per-chain output meters. Pure — the atomics and threading stay in the
// processor. Unit-tested directly (Tests/test_MeterMath.cpp).
namespace MeterMath
{
    // Peak-hold decay applied once per audio block.
    inline constexpr float blockPeakDecay = 0.95f;
    // Slower peak decay applied by the 30 Hz transport timer.
    inline constexpr float timerPeakDecay = 0.92f;

    struct Measurement
    {
        float rms  = 0.0f;   // scaled by gain
        float peak = 0.0f;   // scaled by gain
    };

    // Peak and RMS across every channel over the first numSamples samples,
    // scaled by gain. Returns zeros when numSamples <= 0.
    inline Measurement measure (const juce::AudioBuffer<float>& source, int numSamples, float gain)
    {
        Measurement m;
        if (numSamples <= 0)
            return m;

        float peak = 0.0f;
        double sumSquares = 0.0;
        int counted = 0;

        for (int ch = 0; ch < source.getNumChannels(); ++ch)
        {
            const float* data = source.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
            {
                const float v = data[i];
                peak = juce::jmax (peak, std::abs (v));
                sumSquares += static_cast<double> (v) * static_cast<double> (v);
                ++counted;
            }
        }

        const double rms = counted > 0 ? std::sqrt (sumSquares / static_cast<double> (counted)) : 0.0;
        m.rms  = static_cast<float> (rms) * gain;
        m.peak = peak * gain;
        return m;
    }

    // One-pole smoothing toward `target`; `smoothing` is the time constant
    // in blocks. <= 0 snaps straight to the target.
    inline float smoothLevel (float previous, float target, float smoothing)
    {
        if (smoothing <= 0.0f)
            return target;
        const float alpha = 1.0f / smoothing;
        return previous + (target - previous) * alpha;
    }
}
