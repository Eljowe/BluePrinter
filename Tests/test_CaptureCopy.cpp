#include "TestRunner.h"
#include "CaptureCopy.h"

BP_TEST (CaptureCopy_copiesTheRegionAcrossAllChannels)
{
    juce::AudioBuffer<float> source (2, 8);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 8; ++i)
            source.setSample (ch, i, static_cast<float> (ch * 10 + i));

    auto region = CaptureCopy::copyRegion (source, 2, 4);
    BP_CHECK (region != nullptr);
    BP_CHECK_EQ (region->getNumChannels(), 2);
    BP_CHECK_EQ (region->getNumSamples(), 4);
    BP_CHECK_NEAR (region->getSample (0, 0), 2.0f, 0.0001f);
    BP_CHECK_NEAR (region->getSample (1, 3), 15.0f, 0.0001f);
}

BP_TEST (CaptureCopy_rejectsEmptyOrOutOfBounds)
{
    juce::AudioBuffer<float> source (1, 4);

    BP_CHECK (CaptureCopy::copyRegion (source, 0, 0) == nullptr);
    BP_CHECK (CaptureCopy::copyRegion (source, -1, 2) == nullptr);
    BP_CHECK (CaptureCopy::copyRegion (source, 3, 2) == nullptr);
    BP_CHECK (CaptureCopy::copyRegion (source, 0, 5) == nullptr);
}
