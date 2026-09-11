#pragma once

#include <JuceHeader.h>
#include <atomic>

#include "MeterMath.h"

// One level meter: a smoothed-RMS (or raw-peak) level, a peak-hold value and
// a clip latch. Atomics so the audio thread writes and the UI timer reads.
// The processor keeps four (input / record / output / loop); the per-chain
// output meters share the same math through computeInto().
//
// Unit-tested directly (Tests/test_Meter.cpp).
class Meter
{
public:
    // Smoothed-RMS level + peak-hold + clip latch from a block. Shared with
    // the per-chain meters, which write into their own atomics.
    static void computeInto (const juce::AudioBuffer<float>& source,
                             int numSamples,
                             std::atomic<float>& levelAtomic,
                             std::atomic<float>& peakAtomic,
                             float gain,
                             std::atomic<bool>* clipAtomic = nullptr)
    {
        if (numSamples <= 0)
            return;

        const auto measured = MeterMath::measure (source, numSamples, gain);

        const float prevLevel = levelAtomic.load (std::memory_order_acquire);
        const float prevPeak  = peakAtomic.load  (std::memory_order_acquire);
        const float newLevel  = MeterMath::smoothLevel (prevLevel, measured.rms,
                                                        MeterMath::levelSmoothing);

        levelAtomic.store (newLevel, std::memory_order_release);
        peakAtomic.store  (juce::jmax (measured.peak, prevPeak * MeterMath::blockPeakDecay),
                           std::memory_order_release);

        if (clipAtomic != nullptr && measured.peak >= 1.0f)
            clipAtomic->store (true, std::memory_order_release);
    }

    // Smoothed-RMS level + peak-hold + clip latch from a block.
    void compute (const juce::AudioBuffer<float>& source, int numSamples, float gain)
    {
        computeInto (source, numSamples, level, peak, gain, &clipped);
    }

    // Raw-peak level (no smoothing), peak-hold and clip latch. The looper's
    // playback meter tracks the block peak rather than an RMS.
    void setPeak (float blockPeak)
    {
        const float prevPeak = peak.load (std::memory_order_acquire);
        level.store (blockPeak, std::memory_order_release);
        peak.store (juce::jmax (blockPeak, prevPeak * MeterMath::blockPeakDecay),
                    std::memory_order_release);
        if (blockPeak >= 1.0f)
            clipped.store (true, std::memory_order_release);
    }

    // 30 Hz peak-hold decay.
    void decayPeak()
    {
        const float prev = peak.load (std::memory_order_acquire);
        if (prev > 0.001f)
            peak.store (prev * MeterMath::timerPeakDecay, std::memory_order_release);
    }

    // Slow level fall (used for the loop meter once playback stops).
    void decayLevel (float factor)
    {
        level.store (level.load (std::memory_order_acquire) * factor,
                     std::memory_order_release);
    }

    // Latch the clip indicator (e.g. after an overdub mix pushed the audio
    // past 0 dBFS).
    void latchClip() { clipped.store (true, std::memory_order_release); }
    void resetClip() { clipped.store (false, std::memory_order_release); }

    float getLevel()   const { return level.load   (std::memory_order_acquire); }
    float getPeak()    const { return peak.load    (std::memory_order_acquire); }
    bool  isClipped()  const { return clipped.load (std::memory_order_acquire); }

private:
    std::atomic<float> level   { 0.0f };
    std::atomic<float> peak    { 0.0f };
    std::atomic<bool>  clipped { false };
};
