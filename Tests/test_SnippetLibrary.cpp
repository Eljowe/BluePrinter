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

BP_TEST (SnippetExport_writesLosslessAudioAndOptionallyAppliesGain)
{
    const auto dir = makeTempDir ("export");

    SnippetLibrary library;
    auto snippet = library.addSnippet (makeBuffer (1, 1000, 0.5f), 44100.0, "export me");
    BP_CHECK (snippet != nullptr);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    auto decodedPeak = [&formats](const juce::File& file, juce::int64& outSamples, int& outChannels)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
            return -1.0f;

        outSamples = reader->lengthInSamples;
        outChannels = static_cast<int> (reader->numChannels);

        juce::AudioBuffer<float> buffer (static_cast<int> (reader->numChannels),
                                         static_cast<int> (reader->lengthInSamples));
        reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);

        float peak = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));

        return peak;
    };

    juce::String error;
    juce::int64 samples = 0;
    int channels = 0;

    // Unity: gainDb 0 with applyGain on leaves the peak at ~0.5.
    auto wavFile = dir.getChildFile ("unity.wav");
    BP_CHECK (SnippetLibrary::exportSnippetToFile (*snippet, wavFile, true, error));
    BP_CHECK (error.isEmpty());
    BP_CHECK_NEAR (decodedPeak (wavFile, samples, channels), 0.5, 0.01);
    BP_CHECK_EQ (samples, static_cast<juce::int64> (1000));
    BP_CHECK_EQ (channels, 1);

    // -6.02 dB halves the peak in the exported file...
    snippet->gainDb = -6.0206f;
    auto gainedFile = dir.getChildFile ("gained.wav");
    BP_CHECK (SnippetLibrary::exportSnippetToFile (*snippet, gainedFile, true, error));
    BP_CHECK_NEAR (decodedPeak (gainedFile, samples, channels), 0.25, 0.01);

    // ...but is ignored when applyGain is false (the library-save semantics).
    auto unityFile = dir.getChildFile ("unity-again.wav");
    BP_CHECK (SnippetLibrary::exportSnippetToFile (*snippet, unityFile, false, error));
    BP_CHECK_NEAR (decodedPeak (unityFile, samples, channels), 0.5, 0.01);

    dir.deleteRecursively();
}

BP_TEST (SnippetExport_rejectsUnsupportedExtensions)
{
    const auto dir = makeTempDir ("export-bad");

    SnippetLibrary library;
    auto snippet = library.addSnippet (makeBuffer (1, 100, 0.2f), 44100.0, "x");
    BP_CHECK (snippet != nullptr);

    juce::String error;
    BP_CHECK (! SnippetLibrary::exportSnippetToFile (*snippet, dir.getChildFile ("nope.xyz"), true, error));
    BP_CHECK (error.isNotEmpty());

    dir.deleteRecursively();
}

BP_TEST (SnippetExport_listsSupportedLosslessFormats)
{
    const auto extensions = SnippetLibrary::supportedExportExtensions();
    BP_CHECK (extensions.contains (".wav"));
    // WAV/AIFF/FLAC writers are bundled with JUCE 8.
    BP_CHECK (extensions.contains (".aiff"));
    BP_CHECK (extensions.contains (".flac"));
}

BP_TEST (SnippetImport_fileAndStreamWithDuplicateDetection)
{
    const auto dir = makeTempDir ("import");

    SnippetLibrary library;
    auto source = library.addSnippet (makeBuffer (1, 1000, 0.5f), 44100.0, "source");
    BP_CHECK (source != nullptr);

    // A WAV on disk to import back.
    juce::String error;
    auto wav = dir.getChildFile ("source.wav");
    BP_CHECK (SnippetLibrary::exportSnippetToFile (*source, wav, false, error));

    // File import decodes, computes peaks, and records the source path.
    const int fileId = library.importAudioFile (wav, error);
    BP_CHECK (fileId >= 0);
    BP_CHECK (error.isEmpty());

    auto imported = library.findById (fileId);
    BP_CHECK (imported != nullptr);
    BP_CHECK_EQ (imported->numSamples, static_cast<juce::int64> (1000));
    BP_CHECK_EQ (imported->numChannels, 1);
    BP_CHECK (static_cast<int> (imported->peaks.size()) > 0);
    BP_CHECK (imported->savedPath == wav.getFullPathName());

    // Re-importing the same file is a no-op.
    BP_CHECK_EQ (library.importAudioFile (wav, error), -1);
    BP_CHECK (error.containsIgnoreCase ("already"));

    // Stream import (the drag-drop path) decodes from memory.
    juce::MemoryBlock bytes;
    BP_CHECK (wav.loadFileAsData (bytes));

    const auto sourceKey = "drop:dropped:" + juce::String (bytes.getSize());
    auto stream = std::make_unique<juce::MemoryInputStream> (bytes, false);
    const int streamId = library.importAudioFromStream (std::move (stream), "dropped", sourceKey, error);
    BP_CHECK (streamId >= 0);

    auto dropped = library.findById (streamId);
    BP_CHECK (dropped != nullptr);
    BP_CHECK_EQ (dropped->numSamples, static_cast<juce::int64> (1000));

    // The same source key is rejected.
    auto stream2 = std::make_unique<juce::MemoryInputStream> (bytes, false);
    BP_CHECK_EQ (library.importAudioFromStream (std::move (stream2), "dropped", sourceKey, error), -1);

    // Corrupt data fails with a message rather than crashing.
    juce::MemoryBlock junk;
    junk.append ("not audio", 9);
    auto junkStream = std::make_unique<juce::MemoryInputStream> (junk, false);
    BP_CHECK_EQ (library.importAudioFromStream (std::move (junkStream), "junk.wav", "drop:junk", error), -1);
    BP_CHECK (error.isNotEmpty());

    dir.deleteRecursively();
}

