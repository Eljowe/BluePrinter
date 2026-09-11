#include "TestRunner.h"
#include "MeterMath.h"

BP_TEST (MeterMath_measuresPeakAndRms)
{
    juce::AudioBuffer<float> buf (2, 8);
    buf.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 8; ++i)
            buf.setSample (ch, i, 0.5f);

    auto m = MeterMath::measure (buf, 8, 1.0f);
    BP_CHECK_NEAR (m.peak, 0.5f, 0.0001f);
    BP_CHECK_NEAR (m.rms, 0.5f, 0.0001f);

    // Gain scales both.
    m = MeterMath::measure (buf, 8, 2.0f);
    BP_CHECK_NEAR (m.peak, 1.0f, 0.0001f);
    BP_CHECK_NEAR (m.rms, 1.0f, 0.0001f);

    // A single spike: peak 1, rms 1/sqrt(16).
    juce::AudioBuffer<float> spike (1, 16);
    spike.clear();
    spike.setSample (0, 0, 1.0f);
    m = MeterMath::measure (spike, 16, 1.0f);
    BP_CHECK_NEAR (m.peak, 1.0f, 0.0001f);
    BP_CHECK_NEAR (m.rms, 1.0f / std::sqrt (16.0f), 0.0001f);

    // No samples -> zeros.
    m = MeterMath::measure (buf, 0, 1.0f);
    BP_CHECK_NEAR (m.peak, 0.0f, 0.0001f);
    BP_CHECK_NEAR (m.rms, 0.0f, 0.0001f);
}

BP_TEST (MeterMath_smoothsAndDecays)
{
    BP_CHECK_NEAR (MeterMath::smoothLevel (0.0f, 1.0f, 1.0f), 1.0f, 0.0001f);
    BP_CHECK_NEAR (MeterMath::smoothLevel (0.0f, 1.0f, 2.0f), 0.5f, 0.0001f);
    BP_CHECK_NEAR (MeterMath::smoothLevel (0.5f, 1.0f, 2.0f), 0.75f, 0.0001f);
    BP_CHECK_NEAR (MeterMath::smoothLevel (0.25f, 1.0f, 8.0f), 0.25f + 0.75f / 8.0f, 0.0001f);
    BP_CHECK_NEAR (MeterMath::smoothLevel (0.3f, 1.0f, 0.0f), 1.0f, 0.0001f);

    BP_CHECK_NEAR (MeterMath::blockPeakDecay, 0.95f, 0.0001f);
    BP_CHECK_NEAR (MeterMath::timerPeakDecay, 0.92f, 0.0001f);
}
