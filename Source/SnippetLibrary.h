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
