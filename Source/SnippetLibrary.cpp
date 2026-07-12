#include "SnippetLibrary.h"

namespace
{
juce::String sanitizeForFilename (const juce::String& input)
{
    auto cleaned = input.trim();
    if (cleaned.isEmpty())
        return "snippet";

    static const juce::String invalid = R"(<>:"/\|?*)";
    juce::String result;
    result.preallocateBytes (cleaned.length() * 2);

    for (int i = 0; i < cleaned.length(); ++i)
    {
        auto c = cleaned[i];
        if (c < 32 || invalid.containsChar (c))
            result << '_';
        else
            result << c;
    }

    while (result.contains ("  "))
        result = result.replace ("  ", " ");

    if (result.length() > 80)
        result = result.substring (0, 80);

    if (result.isEmpty())
        return "snippet";

    return result;
}
}

SnippetLibrary::SnippetLibrary() = default;

std::shared_ptr<Snippet> SnippetLibrary::addSnippet (std::shared_ptr<juce::AudioBuffer<float>> audio,
                                                     double sampleRate,
                                                     const juce::String& name)
{
    if (audio == nullptr || audio->getNumSamples() <= 0)
        return nullptr;

    auto snippet = std::make_shared<Snippet>();
    snippet->audio = std::move (audio);
    snippet->sampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    snippet->numChannels = snippet->audio->getNumChannels();
    snippet->numSamples = snippet->audio->getNumSamples();
    snippet->name = name;
    snippet->creationTime = juce::Time::getCurrentTime();
    snippet->peaks = computePeaks (*snippet->audio, peaksPerSnippet);

    {
        const std::lock_guard<std::mutex> lock (mutex);
        snippet->id = nextId++;
        snippets.push_back (snippet);
    }

    return snippet;
}

bool SnippetLibrary::removeSnippet (int id)
{
    const std::lock_guard<std::mutex> lock (mutex);
    for (auto it = snippets.begin(); it != snippets.end(); ++it)
    {
        if ((*it)->id == id)
        {
            snippets.erase (it);
            return true;
        }
    }
    return false;
}

bool SnippetLibrary::updateMeta (int id, const juce::String& name, const juce::String& comments)
{
    const std::lock_guard<std::mutex> lock (mutex);
    for (auto& s : snippets)
    {
        if (s->id == id)
        {
            s->name = name.substring (0, maxNameLength);
            s->comments = comments.substring (0, maxCommentsLength);
            return true;
        }
    }
    return false;
}

bool SnippetLibrary::markSaved (int id, const juce::String& path)
{
    const std::lock_guard<std::mutex> lock (mutex);
    for (auto& s : snippets)
    {
        if (s->id == id)
        {
            s->savedPath = path;
            return true;
        }
    }
    return false;
}

std::shared_ptr<Snippet> SnippetLibrary::findById (int id)
{
    const std::lock_guard<std::mutex> lock (mutex);
    for (auto& s : snippets)
    {
        if (s->id == id)
            return s;
    }
    return nullptr;
}

int SnippetLibrary::indexOfId (int id) const
{
    const std::lock_guard<std::mutex> lock (mutex);
    for (size_t i = 0; i < snippets.size(); ++i)
    {
        if (snippets[i]->id == id)
            return static_cast<int> (i);
    }
    return -1;
}

std::vector<std::shared_ptr<Snippet>> SnippetLibrary::snapshot() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return snippets;
}

int SnippetLibrary::numSnippets() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return static_cast<int> (snippets.size());
}

bool SnippetLibrary::saveSnippetToFolder (const Snippet& snippet,
                                          const juce::File& folder,
                                          juce::String& outPath,
                                          juce::String& outError) const
{
    if (snippet.audio == nullptr)
    {
        outError = "Snippet has no audio data.";
        return false;
    }

    if (! folder.isDirectory() && ! folder.createDirectory())
    {
        outError = "Could not create folder: " + folder.getFullPathName();
        return false;
    }

    auto base = sanitizeForFilename (snippet.name);
    auto timestamp = snippet.creationTime;
    auto stamp = timestamp.formatted ("%Y%m%d-%H%M%S");

    juce::File audioFile = folder.getChildFile (juce::String (base) + "-" + juce::String (snippet.id) + "-" + stamp + ".wav");
    int suffix = 1;
    while (audioFile.existsAsFile())
    {
        audioFile = folder.getChildFile (juce::String (base) + "-" + juce::String (snippet.id) + "-" + stamp + "-" + juce::String (suffix++) + ".wav");
    }

    {
        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::FileOutputStream> stream (audioFile.createOutputStream());
        if (stream == nullptr)
        {
            outError = "Could not open file for writing: " + audioFile.getFullPathName();
            return false;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer (wavFormat.createWriterFor (stream.release(),
                                                                                    snippet.sampleRate,
                                                                                    static_cast<unsigned int> (snippet.numChannels),
                                                                                    16,
                                                                                    {},
                                                                                    0));
        if (writer == nullptr)
        {
            outError = "Could not create WAV writer.";
            return false;
        }

        if (! writer->writeFromAudioSampleBuffer (*snippet.audio, 0, snippet.audio->getNumSamples()))
        {
            outError = "Failed to write audio data.";
            return false;
        }
    }

    auto jsonFile = audioFile.getSiblingFile (audioFile.getFileNameWithoutExtension() + ".json");
    {
        auto* meta = new juce::DynamicObject();
        meta->setProperty ("id", snippet.id);
        meta->setProperty ("name", snippet.name);
        meta->setProperty ("comments", snippet.comments);
        meta->setProperty ("sampleRate", snippet.sampleRate);
        meta->setProperty ("numChannels", snippet.numChannels);
        meta->setProperty ("numSamples", static_cast<double> (snippet.numSamples));
        meta->setProperty ("durationSeconds", snippet.numSamples / snippet.sampleRate);
        meta->setProperty ("createdAt", snippet.creationTime.toISO8601 (true));
        meta->setProperty ("audioFile", audioFile.getFileName());
        meta->setProperty ("format", "WAV 16-bit PCM");

        juce::FileOutputStream stream (jsonFile);
        if (stream.openedOk())
            juce::JSON::writeToStream (stream, juce::var (meta), true);
    }

    outPath = audioFile.getFullPathName();
    return true;
}

std::vector<float> SnippetLibrary::computePeaks (const juce::AudioBuffer<float>& audio, int numBuckets)
{
    std::vector<float> result (static_cast<size_t> (numBuckets), 0.0f);
    if (numBuckets <= 0 || audio.getNumSamples() <= 0)
        return result;

    const int totalSamples = audio.getNumSamples();
    const int numChannels = audio.getNumChannels();
    const double samplesPerBucket = static_cast<double> (totalSamples) / static_cast<double> (numBuckets);

    for (int bucket = 0; bucket < numBuckets; ++bucket)
    {
        const int startSample = static_cast<int> (std::floor (bucket * samplesPerBucket));
        int endSample = static_cast<int> (std::floor ((bucket + 1) * samplesPerBucket));
        if (endSample <= startSample)
            endSample = startSample + 1;
        if (endSample > totalSamples)
            endSample = totalSamples;

        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* data = audio.getReadPointer (ch);
            for (int i = startSample; i < endSample; ++i)
            {
                const float v = std::abs (data[i]);
                if (v > peak)
                    peak = v;
            }
        }

        result[static_cast<size_t> (bucket)] = juce::jlimit (0.0f, 1.0f, peak);
    }

    return result;
}
