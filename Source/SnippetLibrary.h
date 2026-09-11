#pragma once

#include <JuceHeader.h>
#include <memory>
#include <mutex>
#include <vector>

struct Snippet
{
    int id = 0;
    juce::String name;
    juce::String comments;
    double sampleRate = 0.0;
    int numChannels = 0;
    int64_t numSamples = 0;
    std::shared_ptr<const juce::AudioBuffer<float>> audio;
    std::vector<float> peaks;
    juce::Time creationTime;
    juce::String savedPath;
    // Detected musical key, populated by KeyDetector when the user
    // runs "Detect key" on the snippet. Empty string means either no
    // detection has been run yet, or the detector returned no result
    // (audio too short, atonal, or below the confidence threshold).
    juce::String key;
    float        keyConfidence = 0.0f;
    // Pitch classes detected in the snippet (e.g. {"C", "E", "G"}),
    // strongest first. Populated by the same Analyse pass as the key,
    // empty if analysis hasn't run or the audio has no clear pitches.
    juce::StringArray detectedNotes;
    // Organisational colour tag, one of the 8 palette keys the UI
    // offers ("red", "orange", "yellow", "green", "teal", "blue",
    // "purple", "pink"). Empty string = no colour. Persisted in the
    // sidecar JSON.
    juce::String color;
    // Non-destructive playback trim (dB, -24..+24, 0 = unity). Applied to
    // the monitored/played-back audio only — it is never baked into the
    // saved WAV. Persisted in the sidecar JSON.
    float gainDb = 0.0f;
};

class SnippetLibrary
{
public:
    SnippetLibrary();

    std::shared_ptr<Snippet> addSnippet (std::shared_ptr<juce::AudioBuffer<float>> audio,
                                         double sampleRate,
                                         const juce::String& name);

    bool removeSnippet (int id);

    bool updateMeta (int id, const juce::String& name, const juce::String& comments);

    // Set the organisational colour tag on a snippet. `color` should be
    // one of the 8 palette keys the UI offers, or empty to clear it.
    bool updateColor (int id, const juce::String& color);

    // Set the non-destructive playback trim (dB, clamped to -24..+24).
    bool updateGain (int id, float gainDb);

    bool markSaved (int id, const juce::String& path);

    std::shared_ptr<Snippet> findById (int id);

    int indexOfId (int id) const;

    std::vector<std::shared_ptr<Snippet>> snapshot() const;

    int numSnippets() const;

    bool saveSnippetToFolder (const Snippet& snippet,
                              const juce::File& folder,
                              juce::String& outPath,
                              juce::String& outError) const;

    // Scans the given folder for .wav files and adds them to the library.
    // Each .wav may have a matching .json sidecar with metadata; if missing,
    // the basename is used as the snippet name. Files whose absolute path
    // is already in the library (i.e. already loaded) are skipped so picking
    // the same folder twice does not duplicate entries.
    //
    // @param folder    the library folder to scan
    // @param outError  set on fatal errors (folder not found) or when files
    //                  were found but none could be decoded
    // @return true if at least one file was loaded (or the folder was empty)
    bool loadFromFolder (const juce::File& folder, juce::String& outError);

    // Rewrites the .json sidecar for an already-saved snippet so that edits
    // to its name/comments survive a reload. No-op if the snippet has no
    // savedPath or the sidecar cannot be written.
    bool persistMetadata (int id);

    // Removes the .wav and .json sidecar associated with a snippet's
    // savedPath from disk. No-op if there is nothing to remove. Returns
    // true if at least one file was deleted.
    static bool deleteSavedFiles (const Snippet& snippet);

    static std::vector<float> computePeaks (const juce::AudioBuffer<float>& audio, int numBuckets);

    // Writes `snippet` to `file`, choosing the writer from the file's
    // extension (WAV/AIFF/FLAC as the JUCE build provides). When
    // `applyGain` is true the snippet's non-destructive `gainDb` trim is
    // baked into the exported audio — an export is a finished file — while
    // the library's own save stays non-destructive. Returns false and sets
    // outError on any failure.
    static bool exportSnippetToFile (const Snippet& snippet,
                                     const juce::File& file,
                                     bool applyGain,
                                     juce::String& outError);

    // Extensions this build can export to, derived from the registered
    // formats (MP3 has no writer and is never offered). UI order matters:
    // WAV first, then lossless alternatives.
    static juce::StringArray supportedExportExtensions();

private:
    static bool writeMetadataFile (const Snippet& snippet, const juce::File& jsonFile);

    static constexpr int peaksPerSnippet = 256;
    static constexpr int maxNameLength = 80;
    static constexpr int maxCommentsLength = 2000;

private:
    mutable std::mutex mutex;
    std::vector<std::shared_ptr<Snippet>> snippets;
    int nextId = 1;
};
