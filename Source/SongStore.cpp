#include "SongStore.h"

#include <algorithm>

namespace
{
constexpr int maxSongs        = 64;
constexpr int maxSections     = 64;
constexpr int maxTotalBars    = 512;
constexpr int maxChords       = 32;
constexpr int maxNameChars    = 80;
constexpr int maxSectionName  = 40;
constexpr int maxIdeaChars    = 200;
constexpr int maxSymbolChars  = 12;
constexpr int maxChordName    = 40;
constexpr int maxIdChars      = 32;

constexpr int allowedBars[] = { 1, 2, 4, 8, 12, 16 };

bool containsId (const std::vector<juce::String>& ids, const juce::String& id)
{
    return std::find (ids.begin(), ids.end(), id) != ids.end();
}

bool containsInt (const std::vector<int>& ids, int id)
{
    return std::find (ids.begin(), ids.end(), id) != ids.end();
}

int clampInt (int value, int lo, int hi)
{
    return juce::jlimit (lo, hi, value);
}

juce::String trimmedTo (const juce::String& value, int maxChars)
{
    return value.trim().substring (0, maxChars);
}

// Section bars are loopable counts: the nearest allowed value, clamped into
// the 1..16 range. Ties pick the smaller value (deterministic).
int normaliseBars (int bars)
{
    const int target = clampInt (bars, 1, 16);
    int best = allowedBars[0];
    int bestDistance = std::abs (best - target);

    for (int candidate : allowedBars)
    {
        const int distance = std::abs (candidate - target);
        if (distance < bestDistance)
        {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
}

bool isIdChar (juce::juce_wchar c)
{
    return (c >= 'A' && c <= 'Z')
        || (c >= 'a' && c <= 'z')
        || (c >= '0' && c <= '9')
        || c == '_' || c == '-';
}

juce::String sanitiseId (const juce::String& raw)
{
    juce::String result;
    for (auto c : raw)
    {
        if (isIdChar (c))
            result += c;
        if (result.length() >= maxIdChars)
            break;
    }
    return result;
}

std::vector<SongChord> sanitiseChords (const std::vector<SongChord>& chords, int bars)
{
    std::vector<SongChord> result;
    std::vector<int> seenBars;

    for (const auto& chord : chords)
    {
        if (static_cast<int> (result.size()) >= maxChords)
            break;
        if (chord.bar < 0 || chord.bar >= bars || containsInt (seenBars, chord.bar))
            continue;

        auto symbol = trimmedTo (chord.symbol, maxSymbolChars);
        if (symbol.isEmpty())
            continue;

        SongChord entry;
        entry.bar = chord.bar;
        entry.symbol = symbol;
        entry.name = trimmedTo (chord.name, maxChordName);
        result.push_back (std::move (entry));
        seenBars.push_back (chord.bar);
    }

    std::sort (result.begin(), result.end(),
               [] (const SongChord& a, const SongChord& b) { return a.bar < b.bar; });
    return result;
}

float sanitiseRecordedBpm (float bpm)
{
    if (! (bpm > 0.0f))
        return 0.0f;
    return juce::jlimit (20.0f, 300.0f, bpm);
}

// One song, fully sanitised. `usedSectionIds` is mutated so a deterministic
// fallback ("s1", "s2", …) never collides with a valid earlier id.
Song sanitiseSong (const Song& input)
{
    Song song;
    song.id = sanitiseId (input.id);
    song.name = trimmedTo (input.name, maxNameChars);
    song.keyRoot = clampInt (input.keyRoot, 0, 11);
    song.keyScale = input.keyScale == "minor" ? "minor" : "major";
    song.bpm = juce::jlimit (20.0f, 300.0f, input.bpm);

    const int numerator = clampInt (input.numerator, 1, 16);
    song.numerator = numerator;

    switch (input.denominator)
    {
        case 1: case 2: case 4: case 8: case 16:
            song.denominator = input.denominator;
            break;
        default:
            song.denominator = 4;
            break;
    }

    std::vector<juce::String> usedSectionIds;
    int totalBars = 0;

    for (const auto& section : input.sections)
    {
        if (static_cast<int> (song.sections.size()) >= maxSections)
            break;

        const int bars = normaliseBars (section.bars);
        if (totalBars + bars > maxTotalBars)
            break;
        totalBars += bars;

        SongSection entry;
        entry.id = sanitiseId (section.id);
        if (entry.id.isEmpty() || containsId (usedSectionIds, entry.id))
        {
            int n = 1;
            do
            {
                entry.id = "s" + juce::String (n++);
            }
            while (containsId (usedSectionIds, entry.id));
        }
        usedSectionIds.push_back (entry.id);

        entry.name = trimmedTo (section.name, maxSectionName);
        if (entry.name.isEmpty())
            entry.name = "Section " + juce::String (song.sections.size() + 1);

        entry.bars = bars;
        entry.chords = sanitiseChords (section.chords, bars);
        entry.idea = trimmedTo (section.idea, maxIdeaChars);
        entry.snippetId = section.snippetId > 0 ? section.snippetId : 0;
        entry.recordedBpm = sanitiseRecordedBpm (section.recordedBpm);
        entry.recordedBars = clampInt (section.recordedBars, 0, 16);

        song.sections.push_back (std::move (entry));
    }

    return song;
}

juce::String nextNumericId (const std::vector<Song>& songs)
{
    int n = 1;
    for (;;)
    {
        const auto candidate = juce::String (n);
        const bool taken = std::any_of (songs.begin(), songs.end(),
                                        [&candidate] (const Song& s) { return s.id == candidate; });
        if (! taken)
            return candidate;
        ++n;
    }
}

double readDouble (const juce::DynamicObject& object, const char* name, double fallback)
{
    const auto& value = object.getProperty (name);
    return value.isVoid() ? fallback : static_cast<double> (value);
}

int readInt (const juce::DynamicObject& object, const char* name, int fallback)
{
    const auto& value = object.getProperty (name);
    return value.isVoid() ? fallback : static_cast<int> (value);
}

juce::String readString (const juce::DynamicObject& object, const char* name, const juce::String& fallback = {})
{
    const auto& value = object.getProperty (name);
    return value.isVoid() ? fallback : value.toString();
}
} // namespace

void SongStore::clear()
{
    songs.clear();
}

const Song* SongStore::find (const juce::String& id) const
{
    for (const auto& song : songs)
        if (song.id == id)
            return &song;
    return nullptr;
}

bool SongStore::upsert (const Song& input)
{
    if (input.name.trim().isEmpty())
        return false;

    Song source = input;
    source.id = sanitiseId (source.id);
    if (source.id.isEmpty())
        source.id = nextNumericId (songs);

    const bool exists = find (source.id) != nullptr;
    if (! exists && static_cast<int> (songs.size()) >= maxSongs)
        return false;

    auto sanitised = sanitiseSong (source);
    if (sanitised.name.isEmpty())
        return false;

    for (auto& song : songs)
    {
        if (song.id == sanitised.id)
        {
            song = std::move (sanitised);
            return true;
        }
    }

    songs.push_back (std::move (sanitised));
    return true;
}

bool SongStore::remove (const juce::String& id)
{
    for (auto it = songs.begin(); it != songs.end(); ++it)
    {
        if (it->id == id)
        {
            songs.erase (it);
            return true;
        }
    }
    return false;
}

bool SongStore::prune (const std::vector<int>& validSnippetIds)
{
    bool changed = false;

    for (auto& song : songs)
    {
        for (auto& section : song.sections)
        {
            if (section.snippetId > 0 && ! containsInt (validSnippetIds, section.snippetId))
            {
                section.snippetId = 0;
                section.recordedBpm = 0.0f;
                section.recordedBars = 0;
                changed = true;
            }
        }
    }

    return changed;
}

juce::String SongStore::toJson() const
{
    juce::Array<juce::var> arr;

    for (const auto& song : songs)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("version", 1);
        obj->setProperty ("id", song.id);
        obj->setProperty ("name", song.name);
        obj->setProperty ("keyRoot", song.keyRoot);
        obj->setProperty ("keyScale", song.keyScale);
        obj->setProperty ("bpm", static_cast<double> (song.bpm));
        obj->setProperty ("numerator", song.numerator);
        obj->setProperty ("denominator", song.denominator);

        juce::Array<juce::var> sections;
        for (const auto& section : song.sections)
        {
            auto* sectionObj = new juce::DynamicObject();
            sectionObj->setProperty ("id", section.id);
            sectionObj->setProperty ("name", section.name);
            sectionObj->setProperty ("bars", section.bars);

            juce::Array<juce::var> chords;
            for (const auto& chord : section.chords)
            {
                auto* chordObj = new juce::DynamicObject();
                chordObj->setProperty ("bar", chord.bar);
                chordObj->setProperty ("symbol", chord.symbol);
                chordObj->setProperty ("name", chord.name);
                chords.add (juce::var (chordObj));
            }
            sectionObj->setProperty ("chords", juce::var (chords));
            sectionObj->setProperty ("idea", section.idea);
            sectionObj->setProperty ("snippetId", section.snippetId);
            sectionObj->setProperty ("recordedBpm", static_cast<double> (section.recordedBpm));
            sectionObj->setProperty ("recordedBars", section.recordedBars);
            sections.add (juce::var (sectionObj));
        }

        obj->setProperty ("sections", juce::var (sections));
        arr.add (juce::var (obj));
    }

    return juce::JSON::toString (juce::var (arr));
}

void SongStore::fromJson (const juce::String& json)
{
    clear();

    const auto parsed = juce::JSON::parse (json);
    const juce::Array<juce::var>* arr = parsed.getArray();

    // Tolerate a wrapper object ({ "songs": [...] }) as well as the bare
    // array the store writes. Unknown fields are ignored throughout.
    if (arr == nullptr)
        if (const auto* obj = parsed.getDynamicObject())
            arr = obj->getProperty ("songs").getArray();

    if (arr == nullptr)
        return;

    for (const auto& value : *arr)
    {
        const auto* obj = value.getDynamicObject();
        if (obj == nullptr)
            continue;

        Song song;
        song.id = readString (*obj, "id");
        song.name = readString (*obj, "name");
        song.keyRoot = readInt (*obj, "keyRoot", 0);
        song.keyScale = readString (*obj, "keyScale", "major");
        song.bpm = static_cast<float> (readDouble (*obj, "bpm", 120.0));
        song.numerator = readInt (*obj, "numerator", 4);
        song.denominator = readInt (*obj, "denominator", 4);

        if (const auto* sections = obj->getProperty ("sections").getArray())
        {
            for (const auto& sectionValue : *sections)
            {
                const auto* sectionObj = sectionValue.getDynamicObject();
                if (sectionObj == nullptr)
                    continue;

                SongSection section;
                section.id = readString (*sectionObj, "id");
                section.name = readString (*sectionObj, "name");
                section.bars = readInt (*sectionObj, "bars", 8);
                section.idea = readString (*sectionObj, "idea");
                section.snippetId = readInt (*sectionObj, "snippetId", 0);
                section.recordedBpm = static_cast<float> (readDouble (*sectionObj, "recordedBpm", 0.0));
                section.recordedBars = readInt (*sectionObj, "recordedBars", 0);

                if (const auto* chords = sectionObj->getProperty ("chords").getArray())
                {
                    for (const auto& chordValue : *chords)
                    {
                        const auto* chordObj = chordValue.getDynamicObject();
                        if (chordObj == nullptr)
                            continue;

                        SongChord chord;
                        chord.bar = readInt (*chordObj, "bar", 0);
                        chord.symbol = readString (*chordObj, "symbol");
                        chord.name = readString (*chordObj, "name");
                        section.chords.push_back (std::move (chord));
                    }
                }

                song.sections.push_back (std::move (section));
            }
        }

        // The upsert path sanitises and gives a blank/duplicate id a fresh
        // numeric fallback; duplicate restored songs replace the earlier one.
        if (song.name.trim().isNotEmpty())
            upsert (song);
    }
}
