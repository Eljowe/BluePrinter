#include "TestRunner.h"
#include "Tuner.h"

#include <cmath>
#include <vector>

namespace
{
std::vector<float> makeSine (double freq, double sampleRate, int numSamples, float amplitude = 0.5f)
{
    std::vector<float> out (static_cast<size_t> (numSamples), 0.0f);
    for (int i = 0; i < numSamples; ++i)
        out[static_cast<size_t> (i)] = amplitude
            * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * freq
                                            * static_cast<double> (i) / sampleRate));
    return out;
}
}

BP_TEST (Tuner_detectsA4)
{
    const auto samples = makeSine (440.0, 48000.0, 4096);
    const auto r = Tuner::detectPitch (samples.data(), static_cast<int> (samples.size()), 48000.0);
    BP_CHECK_NEAR (r.frequency, 440.0f, 1.0f);
    BP_CHECK (r.confidence > 0.8f);
}

BP_TEST (Tuner_detectsLowEAndHighE)
{
    const auto low = makeSine (82.41, 48000.0, 8192);
    const auto lowReading = Tuner::detectPitch (low.data(), static_cast<int> (low.size()), 48000.0);
    BP_CHECK_NEAR (lowReading.frequency, 82.41f, 0.5f);

    const auto high = makeSine (329.63, 48000.0, 4096);
    const auto highReading = Tuner::detectPitch (high.data(), static_cast<int> (high.size()), 48000.0);
    BP_CHECK_NEAR (highReading.frequency, 329.63f, 0.5f);
}

BP_TEST (Tuner_reportsSilenceAndQuietAsNoPitch)
{
    std::vector<float> silence (4096, 0.0f);
    BP_CHECK_EQ (Tuner::detectPitch (silence.data(), static_cast<int> (silence.size()), 48000.0).frequency, 0.0f);

    const auto quiet = makeSine (440.0, 48000.0, 4096, 1.0e-5f);
    BP_CHECK_EQ (Tuner::detectPitch (quiet.data(), static_cast<int> (quiet.size()), 48000.0).frequency, 0.0f);
}

BP_TEST (Tuner_rejectsInvalidInput)
{
    BP_CHECK_EQ (Tuner::detectPitch (nullptr, 0, 48000.0).frequency, 0.0f);
    const auto samples = makeSine (440.0, 48000.0, 1024);
    BP_CHECK_EQ (Tuner::detectPitch (samples.data(), static_cast<int> (samples.size()), 0.0).frequency, 0.0f);
}

BP_TEST (Tuner_describesNoteAndCents)
{
    juce::String note;
    float cents = 99.0f;

    Tuner::describePitch (440.0f, 440.0f, note, cents);
    BP_CHECK_EQ (note, juce::String ("A4"));
    BP_CHECK_NEAR (cents, 0.0f, 0.05f);

    Tuner::describePitch (220.0f, 440.0f, note, cents);
    BP_CHECK_EQ (note, juce::String ("A3"));

    // ~16 cents flat of A4 stays A4 with a negative deviation.
    Tuner::describePitch (435.9f, 440.0f, note, cents);
    BP_CHECK_EQ (note, juce::String ("A4"));
    BP_CHECK_NEAR (cents, -16.3f, 0.5f);

    // A4 referenced to 432 Hz reads sharp.
    Tuner::describePitch (440.0f, 432.0f, note, cents);
    BP_CHECK_EQ (note, juce::String ("A4"));
    BP_CHECK (cents > 25.0f);
}
