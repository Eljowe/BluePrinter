#include "TestRunner.h"
#include "SnippetLibrary.h"
#include "SnippetMath.h"

namespace
{
juce::File makeTempDir (const juce::String& label)
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("BluePrinterTests")
                   .getChildFile (label);
    dir.deleteRecursively();
    dir.createDirectory();
    return dir;
}

std::shared_ptr<juce::AudioBuffer<float>> makeBuffer (int numChannels, int numSamples, float value)
{
    auto buffer = std::make_shared<juce::AudioBuffer<float>> (numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            buffer->setSample (ch, i, value);

    return buffer;
}
}

BP_TEST (SnippetGain_clampsToTheSupportedRange)
{
    SnippetLibrary library;
    auto snippet = library.addSnippet (makeBuffer (1, 100, 0.1f), 44100.0, "clip");
    BP_CHECK (snippet != nullptr);

    BP_CHECK (library.updateGain (snippet->id, 100.0f));
    BP_CHECK_NEAR (library.findById (snippet->id)->gainDb, 24.0, 1e-6);

    BP_CHECK (library.updateGain (snippet->id, -100.0f));
    BP_CHECK_NEAR (library.findById (snippet->id)->gainDb, -24.0, 1e-6);
}

BP_TEST (SnippetMath_normalizesThePeakToOneDbBelowFullScale)
{
    // Full scale needs -1 dB of trim.
    BP_CHECK_NEAR (SnippetMath::normalizeGainDb (1.0f), -1.0, 1e-4);
    // -6.02 dBFS needs +5.02 dB to reach -1 dBFS.
    BP_CHECK_NEAR (SnippetMath::normalizeGainDb (0.5f), 5.0206, 1e-3);
    // Silence has no meaningful normalization.
    BP_CHECK_NEAR (SnippetMath::normalizeGainDb (0.0f), 0.0, 1e-9);
    BP_CHECK_NEAR (SnippetMath::normalizeGainDb (-0.5f), 0.0, 1e-9);
}

BP_TEST (SnippetComputePeaks_tracksMagnitudeAndClampsToFullScale)
{
    juce::AudioBuffer<float> buffer (1, 1000);
    buffer.clear();
    buffer.setSample (0, 500, 0.5f);
    buffer.setSample (0, 999, 2.0f); // deliberately over full scale

    const auto peaks = SnippetLibrary::computePeaks (buffer, 10);
    BP_CHECK_EQ (static_cast<int> (peaks.size()), 10);

    float maxPeak = 0.0f;
    for (float p : peaks)
        maxPeak = juce::jmax (maxPeak, p);

    BP_CHECK_NEAR (maxPeak, 1.0, 1e-6);  // clamped
    BP_CHECK_NEAR (peaks[5], 0.5, 1e-6); // sample 500 lives in bucket 5
}

BP_TEST (SnippetSidecar_roundTripsMetadataThroughDisk)
{
    const auto dir = makeTempDir ("sidecar");

    SnippetLibrary source;
    auto snippet = source.addSnippet (makeBuffer (1, 2000, 0.25f), 48000.0, "My Take");
    BP_CHECK (snippet != nullptr);

    snippet->comments = "a comment";
    snippet->key = "D major";
    snippet->keyConfidence = 0.8f;
    snippet->detectedNotes.add ("D");
    snippet->detectedNotes.add ("F#");
    snippet->detectedNotes.add ("A");
    snippet->color = "teal";
    snippet->gainDb = -3.5f;

    juce::String path, error;
    BP_CHECK (source.saveSnippetToFolder (*snippet, dir, path, error));
    BP_CHECK (error.isEmpty());
    BP_CHECK (juce::File (path).existsAsFile());

    SnippetLibrary loaded;
    BP_CHECK (loaded.loadFromFolder (dir, error));
    BP_CHECK_EQ (loaded.numSnippets(), 1);

    auto roundTripped = loaded.snapshot().front();
    BP_CHECK_EQ (roundTripped->name, juce::String ("My Take"));
    BP_CHECK_EQ (roundTripped->comments, juce::String ("a comment"));
    BP_CHECK_EQ (roundTripped->key, juce::String ("D major"));
    BP_CHECK_NEAR (roundTripped->keyConfidence, 0.8, 1e-4);
    BP_CHECK_EQ (roundTripped->detectedNotes.size(), 3);
    BP_CHECK (roundTripped->detectedNotes.contains ("F#"));
    BP_CHECK_EQ (roundTripped->color, juce::String ("teal"));
    BP_CHECK_NEAR (roundTripped->gainDb, -3.5, 1e-4);
    BP_CHECK_NEAR (roundTripped->sampleRate, 48000.0, 1e-6);
    BP_CHECK_EQ (roundTripped->numChannels, 1);
    BP_CHECK_EQ (static_cast<int> (roundTripped->numSamples), 2000);

    dir.deleteRecursively();
}
