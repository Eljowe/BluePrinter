#include "TestRunner.h"
#include "KeyDetector.h"

namespace
{
juce::AudioBuffer<float> makeTone (const std::vector<double>& freqs, double sampleRate, int numSamples)
{
    juce::AudioBuffer<float> buffer (1, numSamples);
    buffer.clear();
    auto* data = buffer.getWritePointer (0);

    for (int i = 0; i < numSamples; ++i)
    {
        double sample = 0.0;
        for (double f : freqs)
            sample += std::sin (juce::MathConstants<double>::twoPi * f * static_cast<double> (i) / sampleRate);

        data[i] = static_cast<float> (sample / static_cast<double> (freqs.size()) * 0.5);
    }

    return buffer;
}
}

BP_TEST (KeyDetector_findsCMajorTriad)
{
    const double sampleRate = 44100.0;
    const auto audio = makeTone ({ 261.6256, 329.6276, 391.9954 }, sampleRate,
                                 static_cast<int> (sampleRate * 2.0));

    const auto result = KeyDetector::detectKey (audio, sampleRate);

    BP_CHECK (result.key.isNotEmpty());
    BP_CHECK (result.key.startsWith ("C "));
    BP_CHECK (result.confidence >= 0.5f);
    BP_CHECK (result.detectedNotes.contains ("C"));
    BP_CHECK (result.detectedNotes.contains ("E"));
    BP_CHECK (result.detectedNotes.contains ("G"));
}

BP_TEST (KeyDetector_rejectsTooShortAudio)
{
    juce::AudioBuffer<float> audio (1, 1024);
    audio.clear();

    const auto result = KeyDetector::detectKey (audio, 44100.0);

    BP_CHECK (result.key.isEmpty());
    BP_CHECK_NEAR (result.confidence, 0.0f, 1e-9);
}

BP_TEST (KeyDetector_rejectsSilence)
{
    juce::AudioBuffer<float> audio (2, 16384);
    audio.clear();

    const auto result = KeyDetector::detectKey (audio, 44100.0);

    BP_CHECK (result.key.isEmpty());
}
