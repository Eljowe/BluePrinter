#include "TestRunner.h"
#include "CaptureWrite.h"

namespace
{
juce::AudioBuffer<float> ramp (int channels, int samples, float startValue)
{
    juce::AudioBuffer<float> buffer (channels, samples);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < samples; ++i)
            buffer.setSample (ch, i, startValue + static_cast<float> (i));
    return buffer;
}
}

BP_TEST (CaptureWrite_copiesTheWholeBlockWhenThereIsRoom)
{
    auto dest = ramp (2, 10, 0.0f);
    auto source = ramp (2, 4, 100.0f);

    const auto pos = CaptureWrite::write (dest, 0, source, 4, 10);
    BP_CHECK_EQ (pos, static_cast<int64_t> (4));
    BP_CHECK_NEAR (dest.getSample (0, 0), 100.0f, 0.0001f);
    BP_CHECK_NEAR (dest.getSample (1, 3), 103.0f, 0.0001f);
}

BP_TEST (CaptureWrite_clampsToTheLimit)
{
    auto dest = ramp (1, 10, 0.0f);
    auto source = ramp (1, 4, 100.0f);

    // Only two samples fit before the capacity.
    const auto pos = CaptureWrite::write (dest, 8, source, 4, 10);
    BP_CHECK_EQ (pos, static_cast<int64_t> (10));
    BP_CHECK_NEAR (dest.getSample (0, 8), 100.0f, 0.0001f);
    BP_CHECK_NEAR (dest.getSample (0, 9), 101.0f, 0.0001f);
}

BP_TEST (CaptureWrite_noOpsWhenFullOrPastTheLimit)
{
    auto dest = ramp (1, 10, 0.0f);
    auto source = ramp (1, 4, 100.0f);

    BP_CHECK_EQ (CaptureWrite::write (dest, 10, source, 4, 10), static_cast<int64_t> (10));
    BP_CHECK_EQ (CaptureWrite::write (dest, 12, source, 4, 10), static_cast<int64_t> (12));
    BP_CHECK_NEAR (dest.getSample (0, 0), 0.0f, 0.0001f);
}

BP_TEST (CaptureWrite_copiesOnlySharedChannels)
{
    auto dest = ramp (1, 4, 0.0f);
    auto source = ramp (2, 4, 100.0f);

    CaptureWrite::write (dest, 0, source, 4, 4);
    BP_CHECK_NEAR (dest.getSample (0, 0), 100.0f, 0.0001f);
}
