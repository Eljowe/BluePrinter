#include "TestRunner.h"
#include "Resampler.h"

#include <cmath>

namespace
{
juce::AudioBuffer<float> ramp (int channels, int samples, float start)
{
    juce::AudioBuffer<float> buffer (channels, samples);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < samples; ++i)
            buffer.setSample (ch, i, start + static_cast<float> (ch * 100 + i));
    return buffer;
}

int upwardZeroCrossings (const juce::AudioBuffer<float>& buffer, int channel)
{
    int count = 0;
    for (int i = 1; i < buffer.getNumSamples(); ++i)
        if (buffer.getSample (channel, i - 1) < 0.0f && buffer.getSample (channel, i) >= 0.0f)
            ++count;
    return count;
}
}

BP_TEST (Resampler_resampledLengthRoundsToNearest)
{
    BP_CHECK_EQ (Resampler::resampledLength (100, 44100.0, 44100.0), static_cast<int64_t> (100));
    BP_CHECK_EQ (Resampler::resampledLength (100, 22050.0, 44100.0), static_cast<int64_t> (200));
    BP_CHECK_EQ (Resampler::resampledLength (100, 44100.0, 22050.0), static_cast<int64_t> (50));

    BP_CHECK_EQ (Resampler::resampledLength (0, 44100.0, 44100.0), static_cast<int64_t> (0));
    BP_CHECK_EQ (Resampler::resampledLength (100, 0.0, 44100.0), static_cast<int64_t> (0));
    BP_CHECK_EQ (Resampler::resampledLength (-4, 44100.0, 44100.0), static_cast<int64_t> (0));
}

BP_TEST (Resampler_equalRatesIsACopy)
{
    auto source = ramp (2, 16, 0.0f);
    auto out = Resampler::resample (source, 48000.0, 48000.0);
    BP_CHECK_EQ (out.getNumChannels(), 2);
    BP_CHECK_EQ (out.getNumSamples(), 16);
    BP_CHECK_NEAR (out.getSample (1, 5), source.getSample (1, 5), 0.0001f);
}

BP_TEST (Resampler_invalidSourceReturnsEmpty)
{
    juce::AudioBuffer<float> empty;
    BP_CHECK_EQ (Resampler::resample (empty, 44100.0, 48000.0).getNumSamples(), 0);
}

BP_TEST (Resampler_upsamplingPreservesPitchAndLength)
{
    // 1 s of 440 Hz at 22.05 kHz -> 44.1 kHz should be twice as long and
    // still 440 Hz (a zero-crossing count tolerates the interpolator phase).
    const double sourceRate = 22050.0;
    const double targetRate = 44100.0;
    const int numIn = static_cast<int> (sourceRate);
    juce::AudioBuffer<float> source (1, numIn);
    for (int i = 0; i < numIn; ++i)
        source.setSample (0, i, std::sin (2.0 * juce::MathConstants<double>::pi * 440.0
                                          * static_cast<double> (i) / sourceRate));

    auto out = Resampler::resample (source, sourceRate, targetRate);
    BP_CHECK_EQ (out.getNumSamples(), 2 * numIn);

    const int cycles = upwardZeroCrossings (out, 0);
    BP_CHECK (std::abs (cycles - 440) <= 22);
}

BP_TEST (Resampler_downsamplingPreservesPitchAndLength)
{
    const double sourceRate = 44100.0;
    const double targetRate = 22050.0;
    const int numIn = static_cast<int> (sourceRate);
    juce::AudioBuffer<float> source (1, numIn);
    for (int i = 0; i < numIn; ++i)
        source.setSample (0, i, std::sin (2.0 * juce::MathConstants<double>::pi * 200.0
                                          * static_cast<double> (i) / sourceRate));

    auto out = Resampler::resample (source, sourceRate, targetRate);
    BP_CHECK_EQ (out.getNumSamples(), numIn / 2);

    const int cycles = upwardZeroCrossings (out, 0);
    BP_CHECK (std::abs (cycles - 200) <= 15);
}

BP_TEST (Resampler_mapChannelsEqualWidthIsACopy)
{
    auto source = ramp (2, 8, 0.0f);
    auto out = Resampler::mapChannels (source, 2);
    BP_CHECK_EQ (out.getNumChannels(), 2);
    BP_CHECK_EQ (out.getNumSamples(), 8);
    BP_CHECK_NEAR (out.getSample (1, 3), source.getSample (1, 3), 0.0001f);
}

BP_TEST (Resampler_mapChannelsDuplicatesMonoToEveryTarget)
{
    juce::AudioBuffer<float> source (1, 4);
    source.setSample (0, 0, 0.25f);
    source.setSample (0, 1, -0.5f);

    auto out = Resampler::mapChannels (source, 4);
    BP_CHECK_EQ (out.getNumChannels(), 4);
    for (int ch = 0; ch < 4; ++ch)
        BP_CHECK_NEAR (out.getSample (ch, 1), -0.5f, 0.0001f);
}

BP_TEST (Resampler_mapChannelsAveragesDownToMono)
{
    juce::AudioBuffer<float> source (2, 3);
    source.setSample (0, 0, 1.0f);
    source.setSample (1, 0, 0.0f);
    source.setSample (0, 1, -0.5f);
    source.setSample (1, 1, 0.5f);

    auto out = Resampler::mapChannels (source, 1);
    BP_CHECK_EQ (out.getNumChannels(), 1);
    BP_CHECK_NEAR (out.getSample (0, 0), 0.5f, 0.0001f);
    BP_CHECK_NEAR (out.getSample (0, 1), 0.0f, 0.0001f);
}

BP_TEST (Resampler_mapChannelsFoldsEvenSourcePreservingGroups)
{
    // 4 -> 2: target 0 takes the mean of channels 0 and 2, target 1 of 1 and 3.
    juce::AudioBuffer<float> source (4, 2);
    source.setSample (0, 0, 1.0f);
    source.setSample (2, 0, 0.0f);
    source.setSample (1, 0, 0.25f);
    source.setSample (3, 0, 0.75f);

    auto out = Resampler::mapChannels (source, 2);
    BP_CHECK_EQ (out.getNumChannels(), 2);
    BP_CHECK_NEAR (out.getSample (0, 0), 0.5f, 0.0001f);
    BP_CHECK_NEAR (out.getSample (1, 0), 0.5f, 0.0001f);
}

BP_TEST (Resampler_mapChannelsRejectsEmptySourceOrTarget)
{
    juce::AudioBuffer<float> empty;
    BP_CHECK_EQ (Resampler::mapChannels (empty, 2).getNumSamples(), 0);

    auto source = ramp (1, 4, 0.0f);
    BP_CHECK_EQ (Resampler::mapChannels (source, 0).getNumSamples(), 0);
}
