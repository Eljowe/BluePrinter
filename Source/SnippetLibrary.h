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

    static std::vector<float> computePeaks (const juce::AudioBuffer<float>& audio, int numBuckets);

    static constexpr int peaksPerSnippet = 256;
    static constexpr int maxNameLength = 80;
    static constexpr int maxCommentsLength = 2000;

private:
    mutable std::mutex mutex;
    std::vector<std::shared_ptr<Snippet>> snippets;
    int nextId = 1;
};
