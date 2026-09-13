#include "StemCapture.h"

#include <limits>

namespace
{
juce::String chainStemLabel (const juce::String& chainName, int stemIndex)
{
    auto trimmed = chainName.trim();
    if (trimmed.isNotEmpty())
        return trimmed;
    return "Chain " + juce::String (stemIndex);
}
}

bool StemCapture::arm (const juce::String& newSource,
                       const std::vector<juce::String>& chainNames,
                       int numChannelsToUse, int64_t maxSamples)
{
    clear();

    if (maxSamples <= 0)
        return false;

    const int channels = juce::jmax (1, numChannelsToUse);
    const auto count = static_cast<size_t> (chainNames.size()) + 1u;

    const auto bytesPerStem = static_cast<size_t> (maxSamples)
                            * static_cast<size_t> (channels)
                            * sizeof (float);
    if (bytesPerStem > maxBytes / count)
        return false;

    const auto maxInt = static_cast<int64_t> (std::numeric_limits<int>::max());
    const int samples = static_cast<int> (juce::jmin (maxSamples, maxInt));

    // Reuse the existing buffers when the shape matches so a capture never
    // reallocates; otherwise rebuild once.
    bool shapeMatches = stems.size() == count;
    if (shapeMatches)
        for (const auto& stem : stems)
            if (stem.buffer.getNumChannels() != channels
                || stem.buffer.getNumSamples() != samples)
                shapeMatches = false;

    if (! shapeMatches)
    {
        stems.clear();
        stems.resize (count);
        for (auto& stem : stems)
            stem.buffer.setSize (channels, samples);
    }

    source = newSource;
    capacity = maxSamples;

    stems[0].name = "Dry";
    for (size_t i = 0; i < chainNames.size(); ++i)
        stems[i + 1].name = chainStemLabel (chainNames[i], static_cast<int> (i) + 1);

    for (auto& stem : stems)
        stem.buffer.clear();

    length.store (0, std::memory_order_release);
    armed.store (true, std::memory_order_release);
    return true;
}

void StemCapture::finalise (int64_t newLength)
{
    armed.store (false, std::memory_order_release);
    length.store (juce::jlimit<int64_t> (0, capacity, newLength),
                  std::memory_order_release);
}

void StemCapture::clear()
{
    // Deliberately does not touch the buffers: a capture may still be
    // finishing its last block on the audio thread. arm() zeroes them.
    armed.store (false, std::memory_order_release);
    length.store (0, std::memory_order_release);
    source.clear();
    capacity = 0;
}

void StemCapture::release()
{
    armed.store (false, std::memory_order_release);
    length.store (0, std::memory_order_release);
    source.clear();
    capacity = 0;
    stems.clear();
}

void StemCapture::writeStem (int index, const juce::AudioBuffer<float>& src,
                             int64_t pos, float gain, int numSamples)
{
    if (! armed.load (std::memory_order_acquire))
        return;
    if (index < 0 || index >= static_cast<int> (stems.size()))
        return;
    if (pos < 0 || pos >= capacity || numSamples <= 0)
        return;

    auto& dest = stems[static_cast<size_t> (index)].buffer;
    const auto count = static_cast<int> (juce::jmin<int64_t> (numSamples,
                                                              capacity - pos));
    if (count <= 0)
        return;

    const int channels = juce::jmin (dest.getNumChannels(), src.getNumChannels());
    const int start = static_cast<int> (pos);
    for (int ch = 0; ch < channels; ++ch)
    {
        dest.copyFrom (ch, start, src, ch, 0, count);
        if (gain != 1.0f)
            dest.applyGain (ch, start, count, gain);
    }
}
