#pragma once

#include <JuceHeader.h>
#include <vector>

// Song builder (0065): a song is a planning document — name, key, tempo,
// meter and an ordered list of sections. Each section carries bar count,
// chord events (one per bar at most), a melody/idea line and one linked
// library snippet. Stored as one JSON array in the properties file under
// `songs`. Pure message-thread state — no audio-thread access — so it lives
// outside the processor's hot path and is unit tested
// (Tests/test_SongStore.cpp).
//
// A song is a plan, not a timeline: no audio regions, no mixer, no
// sequencing. See docs/adr/0008-song-builder-is-a-plan.md.
struct SongChord
{
    int bar = 0;
    juce::String symbol, name;
};

struct SongSection
{
    juce::String id, name;
    int bars = 8;
    std::vector<SongChord> chords;
    juce::String idea;
    int snippetId = 0;       // 0 = no linked recording
    float recordedBpm = 0.0f; // 0 = unknown
    int recordedBars = 0;     // 0 = unknown
};

struct Song
{
    juce::String id, name;
    int keyRoot = 0;
    juce::String keyScale = "major";
    float bpm = 120.0f;
    int numerator = 4, denominator = 4;
    std::vector<SongSection> sections;
};

class SongStore
{
public:
    void clear();

    const std::vector<Song>& all() const { return songs; }
    int size() const { return static_cast<int> (songs.size()); }
    const Song* find (const juce::String& id) const;

    // Sanitise every field and insert/replace by id. Returns false when the
    // name is blank or the store is full (64 songs).
    bool upsert (const Song& song);
    bool remove (const juce::String& id);

    // Clear links whose snippet no longer exists (a deleted snippet or a
    // reloaded library folder). Returns true if anything changed.
    bool prune (const std::vector<int>& validSnippetIds);

    juce::String toJson() const;
    void fromJson (const juce::String& json);

private:
    std::vector<Song> songs;
};
