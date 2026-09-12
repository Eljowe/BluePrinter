#include "TestRunner.h"
#include "LoopPlayback.h"

#include <initializer_list>

namespace
{
juce::AudioBuffer<float> makeMono (std::initializer_list<float> values)
{
    juce::AudioBuffer<float> buffer (1, static_cast<int> (values.size()));
    int i = 0;
    for (float v : values)
        buffer.setSample (0, i++, v);
    return buffer;
}
}

BP_TEST (LoopPlayback_rendersOneShotBlock)
{
    auto source = makeMono ({ 0.1f, 0.2f, 0.3f, 0.4f });
    juce::AudioBuffer<float> dest (1, 4);
    dest.clear();

    const auto pos = LoopPlayback::render (dest, source, 0, 4, 0, false, 1.0f, 0);
    BP_CHECK_EQ (pos, static_cast<int64_t> (4));
    for (int i = 0; i < 4; ++i)
        BP_CHECK_NEAR (dest.getSample (0, i), source.getSample (0, i), 0.0001f);
}

BP_TEST (LoopPlayback_wrapsAcrossCycles)
{
    auto source = makeMono ({ 0.1f, 0.2f, 0.3f, 0.4f });
    juce::AudioBuffer<float> dest (1, 6);
    dest.clear();

    // One and a half cycles: 0.1 0.2 0.3 0.4 | 0.1 0.2
    const auto pos = LoopPlayback::render (dest, source, 0, 4, 0, true, 1.0f, 0);
    BP_CHECK_EQ (pos, static_cast<int64_t> (2));
    const float expected[] = { 0.1f, 0.2f, 0.3f, 0.4f, 0.1f, 0.2f };
    for (int i = 0; i < 6; ++i)
        BP_CHECK_NEAR (dest.getSample (0, i), expected[i], 0.0001f);
}

BP_TEST (LoopPlayback_stopsOneShotEarly)
{
    auto source = makeMono ({ 0.1f, 0.2f, 0.3f, 0.4f });
    juce::AudioBuffer<float> dest (1, 6);
    dest.clear();

    // Not looping: only the window is written; the rest of the block stays
    // silent and the returned position lands on the end.
    const auto pos = LoopPlayback::render (dest, source, 0, 4, 0, false, 1.0f, 0);
    BP_CHECK_EQ (pos, static_cast<int64_t> (4));
    for (int i = 0; i < 4; ++i)
        BP_CHECK_NEAR (dest.getSample (0, i), source.getSample (0, i), 0.0001f);
    BP_CHECK_NEAR (dest.getSample (0, 4), 0.0f, 0.0001f);
    BP_CHECK_NEAR (dest.getSample (0, 5), 0.0f, 0.0001f);
}

BP_TEST (LoopPlayback_appliesSeamDeclick)
{
    auto source = makeMono ({ 0.1f, 0.2f, 0.3f, 0.4f });
    juce::AudioBuffer<float> dest (1, 4);
    dest.clear();

    // declick = 1: fade in/out of one sample at the seam; the middle is
    // untouched. Envelope: 0.5 1 1 0.5.
    LoopPlayback::render (dest, source, 0, 4, 0, true, 1.0f, 1);
    BP_CHECK_NEAR (dest.getSample (0, 0), 0.05f, 0.0001f);
    BP_CHECK_NEAR (dest.getSample (0, 1), 0.2f, 0.0001f);
    BP_CHECK_NEAR (dest.getSample (0, 2), 0.3f, 0.0001f);
    BP_CHECK_NEAR (dest.getSample (0, 3), 0.2f, 0.0001f);
}

BP_TEST (LoopPlayback_reportsBlockPeak)
{
    auto source = makeMono ({ 0.1f, -0.5f, 0.2f, 0.3f });
    juce::AudioBuffer<float> dest (1, 4);
    dest.clear();

    float peak = 0.0f;
    LoopPlayback::render (dest, source, 0, 4, 0, false, 2.0f, 0, &peak);
    BP_CHECK_NEAR (peak, 1.0f, 0.0001f);
}

BP_TEST (LoopPlayback_rejectsInvalidInput)
{
    auto source = makeMono ({ 0.1f, 0.2f });
    juce::AudioBuffer<float> dest (1, 4);
    dest.clear();

    // Window past the source: nothing written, position unchanged.
    BP_CHECK_EQ (LoopPlayback::render (dest, source, 0, 4, 2, false, 1.0f, 0),
                 static_cast<int64_t> (2));
    BP_CHECK_EQ (LoopPlayback::render (dest, source, 0, 0, 2, false, 1.0f, 0),
                 static_cast<int64_t> (2));
    BP_CHECK_NEAR (dest.getSample (0, 0), 0.0f, 0.0001f);
}

BP_TEST (LoopPlayback_mixesLayerIntoLoop)
{
    auto buffer = makeMono ({ 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 3.0f, 4.0f });
    LoopPlayback::mixLayer (buffer, 0, 4, 4, 4, 1.0f);

    const float expected[] = { 1.0f, 2.0f, 3.0f, 4.0f, 1.0f, 2.0f, 3.0f, 4.0f };
    for (int i = 0; i < 8; ++i)
        BP_CHECK_NEAR (buffer.getSample (0, i), expected[i], 0.0001f);
}

BP_TEST (LoopPlayback_wrapMixesPartialSecondCycle)
{
    // Layer is half a cycle: 5,6 wrap onto the loop start.
    auto buffer = makeMono ({ 0.0f, 0.0f, 0.0f, 0.0f, 5.0f, 6.0f });
    LoopPlayback::mixLayer (buffer, 0, 4, 4, 2, 1.0f);

    const float expected[] = { 5.0f, 6.0f, 0.0f, 0.0f, 5.0f, 6.0f };
    for (int i = 0; i < 6; ++i)
        BP_CHECK_NEAR (buffer.getSample (0, i), expected[i], 0.0001f);
}

BP_TEST (LoopPlayback_mixLayerAppliesGain)
{
    auto buffer = makeMono ({ 1.0f, 1.0f, 1.0f, 1.0f, 2.0f, 2.0f, 2.0f, 2.0f });
    LoopPlayback::mixLayer (buffer, 0, 4, 4, 4, 0.5f);

    for (int i = 0; i < 8; ++i)
        BP_CHECK_NEAR (buffer.getSample (0, i), 2.0f, 0.0001f);
}

BP_TEST (LoopPlayback_mixLayerRejectsOutOfBounds)
{
    auto buffer = makeMono ({ 1.0f, 1.0f, 1.0f, 1.0f });
    LoopPlayback::mixLayer (buffer, 0, 8, 4, 4, 1.0f);

    for (int i = 0; i < 4; ++i)
        BP_CHECK_NEAR (buffer.getSample (0, i), 1.0f, 0.0001f);
}
