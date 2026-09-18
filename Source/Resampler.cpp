#include "Resampler.h"

#include <cmath>

namespace
{
// LagrangeInterpolator keeps a short history of input samples; an edge-hold
// pad of this many samples lets it read to the end without a fade to zero.
constexpr int interpolationPadding = 8;
}

int64_t Resampler::resampledLength (int64_t numSamples, double sourceRate, double targetRate)
{
    if (numSamples <= 0 || sourceRate <= 0.0 || targetRate <= 0.0)
        return 0;

    const auto scaled = static_cast<double> (numSamples) * targetRate / sourceRate;
    return juce::jmax<int64_t> (1, static_cast<int64_t> (std::llround (scaled)));
}

juce::AudioBuffer<float> Resampler::resample (const juce::AudioBuffer<float>& source,
                                              double sourceRate, double targetRate)
{
    const int channels = source.getNumChannels();
    const int numIn = source.getNumSamples();

    if (channels <= 0 || numIn <= 0 || sourceRate <= 0.0 || targetRate <= 0.0)
    {
        // An invalid rate with real audio still yields a usable copy.
        if (channels > 0 && numIn > 0)
            return source;

        return {};
    }

    const int numOut = static_cast<int> (resampledLength (numIn, sourceRate, targetRate));
    if (numOut <= 0)
        return {};

    juce::AudioBuffer<float> dest (channels, numOut);
    dest.clear();

    if (juce::approximatelyEqual (sourceRate, targetRate))
    {
        for (int ch = 0; ch < channels; ++ch)
            dest.copyFrom (ch, 0, source, ch, 0, numIn);
        return dest;
    }

    // Pad each channel with a short edge-hold so the interpolation kernel
    // never reads past the source and the tail doesn't fade to zero.
    juce::AudioBuffer<float> padded (channels, numIn + interpolationPadding);
    for (int ch = 0; ch < channels; ++ch)
    {
        padded.copyFrom (ch, 0, source, ch, 0, numIn);
        const float edge = source.getSample (ch, numIn - 1);
        for (int i = 0; i < interpolationPadding; ++i)
            padded.setSample (ch, numIn + i, edge);
    }

    const double ratio = sourceRate / targetRate;
    for (int ch = 0; ch < channels; ++ch)
    {
        juce::LagrangeInterpolator interpolator;
        interpolator.process (ratio,
                              padded.getReadPointer (ch),
                              dest.getWritePointer (ch),
                              numOut,
                              padded.getNumSamples(),
                              0);
    }

    return dest;
}

juce::AudioBuffer<float> Resampler::mapChannels (const juce::AudioBuffer<float>& source,
                                                 int targetChannels)
{
    const int srcChannels = source.getNumChannels();
    const int numSamples = source.getNumSamples();

    if (srcChannels <= 0 || numSamples <= 0 || targetChannels <= 0)
        return {};

    juce::AudioBuffer<float> dest (targetChannels, numSamples);
    dest.clear();

    if (srcChannels < targetChannels)
    {
        for (int tc = 0; tc < targetChannels; ++tc)
            dest.copyFrom (tc, 0, source, tc % srcChannels, 0, numSamples);
        return dest;
    }

    // srcChannels >= targetChannels: each target channel is the mean of the
    // source channels that fold onto it, so an even fold keeps the L/R
    // separation instead of collapsing straight to mono.
    for (int tc = 0; tc < targetChannels; ++tc)
    {
        int contributions = 0;
        for (int sc = tc; sc < srcChannels; sc += targetChannels)
            ++contributions;

        if (contributions <= 0)
            continue;

        const float scale = 1.0f / static_cast<float> (contributions);
        for (int i = 0; i < numSamples; ++i)
        {
            float sum = 0.0f;
            for (int sc = tc; sc < srcChannels; sc += targetChannels)
                sum += source.getSample (sc, i);
            dest.setSample (tc, i, sum * scale);
        }
    }

    return dest;
}
