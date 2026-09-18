#pragma once

#include <JuceHeader.h>
#include <cstdint>

// Pure, message-thread sample preparation for loading a library snippet into
// the looper (ticket 0056): sample-rate conversion and channel mapping. Both
// helpers allocate a new buffer and never run on the audio thread; they are
// unit-tested in Tests/test_Resampler.cpp. The rate conversion wraps
// juce::LagrangeInterpolator (no pitch/tempo shift) and is a plain copy when
// the rates match.
namespace Resampler
{
    // The resampled length, in samples, for a `numSamples`-long source at the
    // given rates (rounded to nearest). Returns 0 for invalid input, and at
    // least 1 sample for a non-empty source so a load never produces an empty
    // loop.
    int64_t resampledLength (int64_t numSamples, double sourceRate, double targetRate);

    // Resamples `source` from `sourceRate` to `targetRate`. Equal (or invalid)
    // rates return a copy of the source; an empty source returns an empty
    // buffer of zero channels.
    juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& source,
                                       double sourceRate, double targetRate);

    // Maps `source` onto `targetChannels`. A narrower source is cycled across
    // the target channels (a mono source is duplicated to all of them); a
    // wider source is averaged down, each target channel taking the mean of
    // the source channels whose index modulo `targetChannels` matches it (so
    // an even fold preserves the stereo image). Returns an empty buffer when
    // the source is empty or `targetChannels` <= 0.
    juce::AudioBuffer<float> mapChannels (const juce::AudioBuffer<float>& source,
                                          int targetChannels);
}
