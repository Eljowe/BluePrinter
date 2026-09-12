#include "TestRunner.h"
#include "ChainRouting.h"

namespace
{
juce::AudioBuffer<float> makeBuffer (int channels, int samples, float base)
{
    juce::AudioBuffer<float> buffer (channels, samples);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < samples; ++i)
            buffer.setSample (ch, i, base + static_cast<float> (ch));
    return buffer;
}
}

BP_TEST (ChainRouting_copiesSelectedChannelsOnly)
{
    auto source = makeBuffer (2, 4, 1.0f);   // ch0 = 1, ch1 = 2
    auto dest = makeBuffer (2, 4, 9.0f);     // must be overwritten/cleared

    ChainRouting::copyInputChannels (dest, source, 0b01, 2, 4);
    BP_CHECK_NEAR (dest.getSample (0, 0), 1.0f, 0.0001f);
    BP_CHECK_NEAR (dest.getSample (1, 0), 0.0f, 0.0001f);   // not selected -> silence

    ChainRouting::copyInputChannels (dest, source, 0b11, 2, 4);
    BP_CHECK_NEAR (dest.getSample (0, 0), 1.0f, 0.0001f);
    BP_CHECK_NEAR (dest.getSample (1, 0), 2.0f, 0.0001f);
}

BP_TEST (ChainRouting_silencesChannelsTheBlockLacks)
{
    auto source = makeBuffer (2, 4, 1.0f);
    auto dest = makeBuffer (2, 4, 9.0f);

    // Both channels selected but the block is mono (numChannels = 1): only
    // ch0 is copied, ch1 stays silent.
    ChainRouting::copyInputChannels (dest, source, 0b11, 1, 4);
    BP_CHECK_NEAR (dest.getSample (0, 0), 1.0f, 0.0001f);
    BP_CHECK_NEAR (dest.getSample (1, 0), 0.0f, 0.0001f);
}

BP_TEST (ChainRouting_sumsWithGainAcrossSharedChannels)
{
    auto dest = makeBuffer (2, 4, 1.0f);
    auto source = makeBuffer (2, 4, 2.0f);   // ch0 = 2, ch1 = 3

    ChainRouting::sumInto (dest, source, 0.5f, 4, 2);
    BP_CHECK_NEAR (dest.getSample (0, 0), 1.0f + 1.0f, 0.0001f);
    BP_CHECK_NEAR (dest.getSample (1, 0), 2.0f + 1.5f, 0.0001f);

    // Gain 0 leaves the mix untouched.
    auto two = makeBuffer (2, 4, 5.0f);
    ChainRouting::sumInto (two, source, 0.0f, 4, 2);
    BP_CHECK_NEAR (two.getSample (0, 0), 5.0f, 0.0001f);
}

BP_TEST (ChainRouting_sumsOnlySharedChannels)
{
    auto dest = makeBuffer (1, 4, 0.0f);
    auto source = makeBuffer (2, 4, 2.0f);

    ChainRouting::sumInto (dest, source, 1.0f, 4, 2);
    BP_CHECK_NEAR (dest.getSample (0, 0), 2.0f, 0.0001f);
}

BP_TEST (ChainRouting_sumClampsToTheBlockChannels)
{
    auto dest = makeBuffer (2, 4, 0.0f);
    auto source = makeBuffer (2, 4, 3.0f);

    // Mono block: only ch0 is summed even though both buffers have two.
    ChainRouting::sumInto (dest, source, 1.0f, 4, 1);
    BP_CHECK_NEAR (dest.getSample (0, 0), 3.0f, 0.0001f);
    BP_CHECK_NEAR (dest.getSample (1, 0), 1.0f, 0.0001f);   // untouched
}
