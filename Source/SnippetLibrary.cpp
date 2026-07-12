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
    writeMetadataFile (snippet, jsonFile);

    outPath = audioFile.getFullPathName();
    return true;
}

bool SnippetLibrary::loadFromFolder (const juce::File& folder, juce::String& outError)
{
    if (! folder.isDirectory())
    {
        outError = "Folder does not exist: " + folder.getFullPathName();
        return false;
    }

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    auto wavFiles = folder.findChildFiles (juce::File::findFiles, false, "*.wav");
    if (wavFiles.isEmpty())
        return true;

    // Snapshot already-loaded paths and the current max id so we can skip
    // duplicates and avoid assigning new ids that collide with existing ones.
    juce::StringArray existingPaths;
    int maxExistingId = 0;
    {
        const std::lock_guard<std::mutex> lock (mutex);
        for (const auto& s : snippets)
        {
            if (s->savedPath.isNotEmpty())
                existingPaths.addIfNotAlreadyThere (s->savedPath);
            maxExistingId = juce::jmax (maxExistingId, s->id);
        }
    }

    int loaded = 0;
    int maxAssignedId = maxExistingId;

    for (const auto& audioFile : wavFiles)
    {
        const auto fullPath = audioFile.getFullPathName();
        if (existingPaths.contains (fullPath))
            continue;

        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (audioFile));
        if (reader == nullptr)
            continue;

        const int numChannels = static_cast<int> (reader->numChannels);
        const int numSamples  = static_cast<int> (reader->lengthInSamples);
        if (numChannels <= 0 || numSamples <= 0)
            continue;

        auto buffer = std::make_shared<juce::AudioBuffer<float>> (numChannels, numSamples);
        if (! reader->read (buffer.get(), 0, numSamples, 0, true, true))
            continue;

        // Read the sidecar JSON if it exists. Missing or malformed JSON is
        // non-fatal: we just fall back to defaults.
        juce::String name = audioFile.getFileNameWithoutExtension();
        juce::String comments;
        int idFromJson = 0;
        juce::Time creationTime;
        bool hasJson = false;

        auto jsonFile = audioFile.getSiblingFile (audioFile.getFileNameWithoutExtension() + ".json");
        if (jsonFile.existsAsFile())
        {
            auto parsed = juce::JSON::parse (jsonFile.loadFileAsString());
            if (auto* obj = parsed.getDynamicObject())
            {
                hasJson = true;
                auto nameFromJson = obj->getProperty ("name").toString();
                if (nameFromJson.isNotEmpty())
                    name = nameFromJson;
                comments = obj->getProperty ("comments").toString();
                idFromJson = static_cast<int> (obj->getProperty ("id"));

                auto createdAtStr = obj->getProperty ("createdAt").toString();
                if (createdAtStr.isNotEmpty())
                {
                    auto parsedTime = juce::Time::fromISO8601 (createdAtStr);
                    if (parsedTime.toMilliseconds() > 0)
                        creationTime = parsedTime;
                }
            }
        }

        auto snippet = std::make_shared<Snippet>();
        snippet->audio        = buffer;
        snippet->sampleRate   = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
        snippet->numChannels  = numChannels;
        snippet->numSamples   = numSamples;
        snippet->name         = name;
        snippet->comments     = comments;
        snippet->savedPath    = fullPath;
        snippet->creationTime = (hasJson && creationTime.toMilliseconds() > 0)
                                    ? creationTime
                                    : juce::Time::getCurrentTime();
        snippet->peaks        = computePeaks (*buffer, peaksPerSnippet);

        {
            const std::lock_guard<std::mutex> lock (mutex);
            snippet->id = (hasJson && idFromJson > 0) ? idFromJson : nextId++;
            maxAssignedId = juce::jmax (maxAssignedId, snippet->id);
            snippets.push_back (snippet);
        }

        existingPaths.addIfNotAlreadyThere (fullPath);
        ++loaded;
    }

    // Make sure future addSnippet() calls don't collide with anything we
    // just pulled in from disk.
    {
        const std::lock_guard<std::mutex> lock (mutex);
        if (nextId <= maxAssignedId)
            nextId = maxAssignedId + 1;
    }

    if (loaded == 0)
    {
        outError = "Found " + juce::String (wavFiles.size())
                 + " WAV file(s) in the folder but could not decode any of them.";
        return false;
    }

    return true;
}

bool SnippetLibrary::persistMetadata (int id)
{
    std::shared_ptr<Snippet> snippet;
    {
        const std::lock_guard<std::mutex> lock (mutex);
        for (auto& s : snippets)
        {
            if (s->id == id)
            {
                snippet = s;
                break;
            }
        }
    }

    if (snippet == nullptr || snippet->savedPath.isEmpty())
        return false;

    juce::File audioFile (snippet->savedPath);
    auto jsonFile = audioFile.getSiblingFile (audioFile.getFileNameWithoutExtension() + ".json");
    return writeMetadataFile (*snippet, jsonFile);
}

bool SnippetLibrary::deleteSavedFiles (const Snippet& snippet)
{
    if (snippet.savedPath.isEmpty())
        return false;

    bool anyDeleted = false;
    juce::File audioFile (snippet.savedPath);
    if (audioFile.existsAsFile())
        anyDeleted = audioFile.deleteFile() || anyDeleted;

    auto jsonFile = audioFile.getSiblingFile (audioFile.getFileNameWithoutExtension() + ".json");
    if (jsonFile.existsAsFile())
        anyDeleted = jsonFile.deleteFile() || anyDeleted;

    return anyDeleted;
}

bool SnippetLibrary::writeMetadataFile (const Snippet& snippet, const juce::File& jsonFile)
{
    auto* meta = new juce::DynamicObject();
    meta->setProperty ("id", snippet.id);
    meta->setProperty ("name", snippet.name);
    meta->setProperty ("comments", snippet.comments);
    meta->setProperty ("sampleRate", snippet.sampleRate);
    meta->setProperty ("numChannels", snippet.numChannels);
    meta->setProperty ("numSamples", static_cast<double> (snippet.numSamples));
    meta->setProperty ("durationSeconds", snippet.sampleRate > 0.0 ? snippet.numSamples / snippet.sampleRate : 0.0);
    meta->setProperty ("createdAt", snippet.creationTime.toISO8601 (true));
    meta->setProperty ("audioFile", jsonFile.getSiblingFile (jsonFile.getFileNameWithoutExtension() + ".wav").getFileName());
    meta->setProperty ("format", "WAV 16-bit PCM");

    juce::FileOutputStream stream (jsonFile);
    if (! stream.openedOk())
        return false;
    juce::JSON::writeToStream (stream, juce::var (meta), true);
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
