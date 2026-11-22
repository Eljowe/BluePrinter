#include "TestRunner.h"
#include "SongStore.h"

namespace
{
Song makeSong (const juce::String& id, const juce::String& name)
{
    Song song;
    song.id = id;
    song.name = name;
    song.keyRoot = 9;
    song.keyScale = "minor";
    song.bpm = 120.0f;
    song.numerator = 4;
    song.denominator = 4;

    SongSection section;
    section.id = "s1";
    section.name = "Intro";
    section.bars = 4;
    section.idea = "clean";
    section.lyrics = "hold the line\nuntil the chorus";
    section.snippetId = 7;
    section.recordedBpm = 118.0f;
    section.recordedBars = 4;

    SongChord chord;
    chord.bar = 0;
    chord.symbol = "Am";
    chord.name = "A minor";
    section.chords.push_back (chord);

    song.sections.push_back (section);
    return song;
}
} // namespace

BP_TEST (SongStore_upsertFindRemove)
{
    SongStore store;
    BP_CHECK (store.upsert (makeSong ("k3j9x2", "Neon Sunday")));
    BP_CHECK_EQ (store.size(), 1);

    const auto* found = store.find ("k3j9x2");
    BP_CHECK (found != nullptr);
    if (found != nullptr)
    {
        BP_CHECK_EQ (found->name, juce::String ("Neon Sunday"));
        BP_CHECK_EQ (found->keyRoot, 9);
        BP_CHECK_EQ (found->keyScale, juce::String ("minor"));
        BP_CHECK_EQ (found->sections.size(), static_cast<size_t> (1));
        BP_CHECK_EQ (found->sections[0].chords.size(), static_cast<size_t> (1));
        BP_CHECK_EQ (found->sections[0].snippetId, 7);
        BP_CHECK_EQ (found->sections[0].lyrics, juce::String ("hold the line\nuntil the chorus"));
    }

    // Same id replaces in place.
    auto renamed = makeSong ("k3j9x2", "Neon Sunday II");
    BP_CHECK (store.upsert (renamed));
    BP_CHECK_EQ (store.size(), 1);
    BP_CHECK_EQ (store.find ("k3j9x2")->name, juce::String ("Neon Sunday II"));

    BP_CHECK (store.remove ("k3j9x2"));
    BP_CHECK_EQ (store.size(), 0);
    BP_CHECK (! store.remove ("k3j9x2"));
    BP_CHECK (store.find ("k3j9x2") == nullptr);

    // A blank name is refused.
    BP_CHECK (! store.upsert (makeSong ("blank", "   ")));
    BP_CHECK_EQ (store.size(), 0);
}

BP_TEST (SongStore_jsonRoundTrips)
{
    SongStore source;
    auto song = makeSong ("songA", "Neon Sunday");
    SongSection chorus;
    chorus.id = "s2";
    chorus.name = "Chorus";
    chorus.bars = 8;
    chorus.idea = "big";
    chorus.snippetId = 0;
    chorus.recordedBpm = 0.0f;
    chorus.recordedBars = 0;
    SongChord chord;
    chord.bar = 4;
    chord.symbol = "F";
    chord.name = "F major";
    chorus.chords.push_back (chord);
    song.sections.push_back (chorus);
    BP_CHECK (source.upsert (song));

    const auto json = source.toJson();
    const auto parsed = juce::JSON::parse (json);
    const auto* arr = parsed.getArray();
    BP_CHECK (arr != nullptr);
    if (arr != nullptr && ! arr->isEmpty())
    {
        const auto* obj = arr->getReference (0).getDynamicObject();
        BP_CHECK (obj != nullptr);
        if (obj != nullptr)
            BP_CHECK_EQ (static_cast<int> (obj->getProperty ("version")), 1);
    }

    SongStore loaded;
    loaded.fromJson (json);
    BP_CHECK_EQ (loaded.size(), 1);

    const auto* round = loaded.find ("songA");
    BP_CHECK (round != nullptr);
    if (round != nullptr)
    {
        BP_CHECK_EQ (round->name, juce::String ("Neon Sunday"));
        BP_CHECK_EQ (round->keyRoot, 9);
        BP_CHECK_EQ (round->keyScale, juce::String ("minor"));
        BP_CHECK_NEAR (round->bpm, 120.0, 0.001);
        BP_CHECK_EQ (round->numerator, 4);
        BP_CHECK_EQ (round->denominator, 4);
        BP_CHECK_EQ (round->sections.size(), static_cast<size_t> (2));

        if (round->sections.size() == 2)
        {
            BP_CHECK_EQ (round->sections[0].id, juce::String ("s1"));
            BP_CHECK_EQ (round->sections[0].bars, 4);
            BP_CHECK_EQ (round->sections[0].idea, juce::String ("clean"));
            BP_CHECK_EQ (round->sections[0].lyrics, juce::String ("hold the line\nuntil the chorus"));
            BP_CHECK_EQ (round->sections[0].snippetId, 7);
            BP_CHECK_NEAR (round->sections[0].recordedBpm, 118.0, 0.001);
            BP_CHECK_EQ (round->sections[0].chords.size(), static_cast<size_t> (1));
            BP_CHECK_EQ (round->sections[0].chords[0].symbol, juce::String ("Am"));
            BP_CHECK_EQ (round->sections[1].name, juce::String ("Chorus"));
            BP_CHECK_EQ (round->sections[1].bars, 8);
            BP_CHECK (round->sections[1].lyrics.isEmpty());
            BP_CHECK_EQ (round->sections[1].chords[0].bar, 4);
        }
    }
}

BP_TEST (SongStore_sanitisesFields)
{
    SongStore store;
    Song song = makeSong ("clean", juce::String::repeatedString ("x", 100));
    song.keyRoot = 99;
    song.keyScale = "dorian";
    song.bpm = 900.0f;
    song.numerator = 0;
    song.denominator = 3;

    SongSection section;
    section.id = "  bad id!  ";
    section.name = "";
    section.bars = 3; // nearest allowed: 2
    section.idea = juce::String::repeatedString ("i", 300);
    // 2101 chars with a line break at 1000: capped at 2000, newline kept.
    section.lyrics = juce::String::repeatedString ("l", 1000)
                   + "\n"
                   + juce::String::repeatedString ("y", 1100);
    section.snippetId = -4;
    section.recordedBpm = 5.0f;   // clamps into 20..300
    section.recordedBars = 99;
    song.sections.push_back (section);

    BP_CHECK (store.upsert (song));
    const auto* saved = store.find ("clean");
    BP_CHECK (saved != nullptr);
    if (saved == nullptr)
        return;

    BP_CHECK_EQ (saved->name.length(), 80);
    BP_CHECK_EQ (saved->keyRoot, 11);
    BP_CHECK_EQ (saved->keyScale, juce::String ("major"));
    BP_CHECK_NEAR (saved->bpm, 300.0, 0.001);
    BP_CHECK_EQ (saved->numerator, 1);
    BP_CHECK_EQ (saved->denominator, 4);

    BP_CHECK_EQ (saved->sections.size(), static_cast<size_t> (2));
    if (saved->sections.size() == 2)
    {
        const auto& clean = saved->sections[1];
        BP_CHECK_EQ (clean.bars, 2);
        BP_CHECK_EQ (clean.name, juce::String ("Section 2"));
        BP_CHECK_EQ (clean.idea.length(), 200);
        BP_CHECK_EQ (clean.lyrics.length(), 2000);
        BP_CHECK_EQ (clean.lyrics.indexOfChar ('\n'), 1000);
        BP_CHECK (clean.lyrics.contains ("\n"));
        BP_CHECK_EQ (clean.snippetId, 0);
        BP_CHECK_NEAR (clean.recordedBpm, 20.0, 0.001);
        BP_CHECK_EQ (clean.recordedBars, 16);
        BP_CHECK (clean.id.isNotEmpty());
        BP_CHECK_EQ (clean.id, juce::String ("badid"));
    }
}

BP_TEST (SongStore_sanitisesChords)
{
    SongStore store;
    Song song = makeSong ("chords", "Chords");
    auto& section = song.sections[0];
    section.bars = 4;
    section.chords.clear();

    const int bars[] = { 3, 0, 0, 9, 2, 1 };
    const char* symbols[] = { "G", "Am", "C", "X", "F", "", "Dm" };
    for (int i = 0; i < 6; ++i)
    {
        SongChord chord;
        chord.bar = bars[i];
        chord.symbol = symbols[i];
        chord.name = juce::String::repeatedString ("n", 60);
        section.chords.push_back (chord);
    }

    BP_CHECK (store.upsert (song));
    const auto* saved = store.find ("chords");
    BP_CHECK (saved != nullptr);
    if (saved == nullptr)
        return;

    const auto& chords = saved->sections[0].chords;
    BP_CHECK_EQ (chords.size(), static_cast<size_t> (3));
    if (chords.size() == 3)
    {
        // Sorted, unique, out-of-range and blank dropped.
        BP_CHECK_EQ (chords[0].bar, 0);
        BP_CHECK_EQ (chords[0].symbol, juce::String ("Am"));
        BP_CHECK_EQ (chords[1].bar, 2);
        BP_CHECK_EQ (chords[1].symbol, juce::String ("F"));
        BP_CHECK_EQ (chords[2].bar, 3);
        BP_CHECK_EQ (chords[2].symbol, juce::String ("G"));
        BP_CHECK_EQ (chords[2].name.length(), 40);
    }

    // More than 32 chord events are truncated.
    Song many = makeSong ("many", "Many");
    many.sections[0].bars = 16;
    many.sections[0].chords.clear();
    for (int bar = 0; bar < 16; ++bar)
    {
        for (int copy = 0; copy < 3; ++copy)
        {
            SongChord chord;
            chord.bar = bar;
            chord.symbol = "C";
            many.sections[0].chords.push_back (chord);
        }
    }
    BP_CHECK (store.upsert (many));
    BP_CHECK (store.find ("many")->sections[0].chords.size() <= static_cast<size_t> (32));
}

BP_TEST (SongStore_enforcesLimits)
{
    SongStore store;

    // 64 songs maximum.
    for (int i = 0; i < 64; ++i)
        BP_CHECK (store.upsert (makeSong ("song" + juce::String (i), "Song " + juce::String (i))));
    BP_CHECK_EQ (store.size(), 64);
    BP_CHECK (! store.upsert (makeSong ("song64", "Too many")));
    BP_CHECK_EQ (store.size(), 64);

    // 512 total bars per song: 64 sections of 16 would be 1024.
    SongStore barStore;
    Song wide = makeSong ("wide", "Wide");
    wide.sections.clear();
    for (int i = 0; i < 64; ++i)
    {
        SongSection section;
        section.id = "s" + juce::String (i);
        section.name = "S" + juce::String (i);
        section.bars = 16;
        wide.sections.push_back (section);
    }
    BP_CHECK (barStore.upsert (wide));
    const auto* saved = barStore.find ("wide");
    BP_CHECK (saved != nullptr);
    if (saved != nullptr)
    {
        int total = 0;
        for (const auto& section : saved->sections)
            total += section.bars;
        BP_CHECK_EQ (total, 512);
        BP_CHECK_EQ (saved->sections.size(), static_cast<size_t> (32));
    }

    // 64 sections maximum.
    SongStore sectionStore;
    Song tall = makeSong ("tall", "Tall");
    tall.sections.clear();
    for (int i = 0; i < 65; ++i)
    {
        SongSection section;
        section.id = "s" + juce::String (i);
        section.name = "S" + juce::String (i);
        section.bars = 1;
        tall.sections.push_back (section);
    }
    BP_CHECK (sectionStore.upsert (tall));
    BP_CHECK_EQ (sectionStore.find ("tall")->sections.size(), static_cast<size_t> (64));
}

BP_TEST (SongStore_pruneClearsMissingLinks)
{
    SongStore store;
    auto song = makeSong ("linked", "Linked");
    SongSection second;
    second.id = "s2";
    second.name = "Verse";
    second.bars = 8;
    second.snippetId = 9;
    second.recordedBpm = 120.0f;
    second.recordedBars = 8;
    song.sections.push_back (second);
    BP_CHECK (store.upsert (song));

    // 7 survives, 9 is gone.
    BP_CHECK (store.prune ({ 7, 11 }));
    const auto* saved = store.find ("linked");
    BP_CHECK (saved != nullptr);
    if (saved != nullptr)
    {
        BP_CHECK_EQ (saved->sections[0].snippetId, 7);
        BP_CHECK_EQ (saved->sections[1].snippetId, 0);
        BP_CHECK_NEAR (saved->sections[1].recordedBpm, 0.0, 0.001);
        BP_CHECK_EQ (saved->sections[1].recordedBars, 0);
    }

    BP_CHECK (! store.prune ({ 7, 11 })); // idempotent
}

BP_TEST (SongStore_idsFallbackAndDuplicateSongsReplace)
{
    SongStore store;

    // Empty song ids get a numeric fallback.
    auto a = makeSong ("", "First");
    auto b = makeSong ("", "Second");
    BP_CHECK (store.upsert (a));
    BP_CHECK (store.upsert (b));
    BP_CHECK_EQ (store.size(), 2);
    BP_CHECK_EQ (store.all()[0].id, juce::String ("1"));
    BP_CHECK_EQ (store.all()[1].id, juce::String ("2"));

    // Empty and duplicate section ids get deterministic fallbacks.
    SongStore sectionStore;
    Song song = makeSong ("sections", "Sections");
    song.sections.clear();
    for (int i = 0; i < 3; ++i)
    {
        SongSection section;
        section.id = i == 2 ? "dup" : "dup";
        section.name = "S" + juce::String (i);
        section.bars = 1;
        song.sections.push_back (section);
    }
    song.sections[1].id = "";
    BP_CHECK (sectionStore.upsert (song));
    const auto* saved = sectionStore.find ("sections");
    BP_CHECK (saved != nullptr);
    if (saved != nullptr)
    {
        std::vector<juce::String> ids;
        for (const auto& section : saved->sections)
        {
            BP_CHECK (section.id.isNotEmpty());
            for (const auto& existing : ids)
                BP_CHECK (existing != section.id);
            ids.push_back (section.id);
        }
        BP_CHECK_EQ (saved->sections[0].id, juce::String ("dup"));
    }

    // Duplicate song ids in JSON: the later document replaces the earlier.
    SongStore loaded;
    loaded.fromJson ("[{\"id\":\"x\",\"name\":\"Old\"},{\"id\":\"x\",\"name\":\"New\"}]");
    BP_CHECK_EQ (loaded.size(), 1);
    BP_CHECK_EQ (loaded.find ("x")->name, juce::String ("New"));
}

BP_TEST (SongStore_jsonTolerantOfGarbageAndVersions)
{
    SongStore store;
    store.fromJson ("not json at all");
    BP_CHECK_EQ (store.size(), 0);

    store.fromJson ("[{\"id\":\"v\",\"name\":\"Versioned\",\"version\":99,\"unknown\":true,"
                    "\"sections\":[{\"id\":\"s1\",\"name\":\"Verse\",\"bars\":7,"
                    "\"chords\":[{\"bar\":0,\"symbol\":\"C\",\"name\":\"C major\"}]}]}]");
    BP_CHECK_EQ (store.size(), 1);
    const auto* saved = store.find ("v");
    BP_CHECK (saved != nullptr);
    if (saved != nullptr)
    {
        BP_CHECK_EQ (saved->name, juce::String ("Versioned"));
        BP_CHECK_EQ (saved->sections.size(), static_cast<size_t> (1));
        BP_CHECK_EQ (saved->sections[0].bars, 8); // 7 normalises to 8
        BP_CHECK_EQ (saved->sections[0].chords.size(), static_cast<size_t> (1));
        BP_CHECK (saved->sections[0].lyrics.isEmpty()); // absent in the JSON
    }

    // A wrapper object is tolerated too.
    SongStore wrapped;
    wrapped.fromJson ("{\"songs\":[{\"name\":\"Wrapped\"}]}");
    BP_CHECK_EQ (wrapped.size(), 1);
    BP_CHECK (wrapped.all()[0].id.isNotEmpty());
}
