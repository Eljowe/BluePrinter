#include "TestRunner.h"
#include "Meter.h"

static juce::AudioBuffer<float> constantBuffer (float value, int numSamples = 64)
{
    juce::AudioBuffer<float> buf (1, numSamples);
    for (int i = 0; i < numSamples; ++i)
        buf.setSample (0, i, value);
    return buf;
}

BP_TEST (Meter_computesSmoothedLevelPeakAndClip)
{
    Meter m;

    // First block: the level one-pole starts at 0 and moves 1/8 toward the
    // RMS (0.5), so it lands at 0.0625; the peak is exact.
    auto buf = constantBuffer (0.5f);
    m.compute (buf, buf.getNumSamples(), 1.0f);
    BP_CHECK_NEAR (m.getLevel(), 0.0625f, 0.0001f);
    BP_CHECK_NEAR (m.getPeak(), 0.5f, 0.0001f);
    BP_CHECK (! m.isClipped());

    // A full-scale sample latches the clip indicator and the peak holds.
    auto loud = constantBuffer (1.0f);
    m.compute (loud, loud.getNumSamples(), 1.0f);
    BP_CHECK (m.isClipped());
    BP_CHECK_NEAR (m.getPeak(), 1.0f, 0.0001f);

    m.resetClip();
    BP_CHECK (! m.isClipped());
}

BP_TEST (Meter_setPeakTracksRawPeakWithHold)
{
    Meter m;
    m.setPeak (0.7f);
    BP_CHECK_NEAR (m.getLevel(), 0.7f, 0.0001f);
    BP_CHECK_NEAR (m.getPeak(), 0.7f, 0.0001f);

    // A quieter block keeps the decayed previous peak.
    m.setPeak (0.3f);
    BP_CHECK_NEAR (m.getLevel(), 0.3f, 0.0001f);
    BP_CHECK_NEAR (m.getPeak(), 0.7f * 0.95f, 0.0001f);

    m.setPeak (1.0f);
    BP_CHECK (m.isClipped());
}

BP_TEST (Meter_decaysPeakAndLevel)
{
    Meter m;
    m.setPeak (1.0f);
    m.decayPeak();
    BP_CHECK_NEAR (m.getPeak(), 0.92f, 0.0001f);

    m.setPeak (0.5f);
    m.decayLevel (0.85f);
    BP_CHECK_NEAR (m.getLevel(), 0.425f, 0.0001f);

    m.latchClip();
    BP_CHECK (m.isClipped());
}
