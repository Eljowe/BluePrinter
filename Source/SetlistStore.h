#pragma once

#include <JuceHeader.h>
#include <vector>

// Named setlists (0035): ordered lists of snippet ids, stored as one JSON
// array in the properties file under `setlists`. Pure message-thread state —
// no audio-thread access — so it lives outside the processor's hot path and
// is unit tested (Tests/test_SetlistStore.cpp).
//
// A setlist's `id` is a stable numeric string ("1", "2", …) so reordering and
// renaming survive a restart; `snippetIds` is ordered and duplicate-free.
struct SetlistEntry
{
    juce::String id;
    juce::String name;
    std::vector<int> snippetIds;
};

class SetlistStore
{
public:
    void clear();

    const std::vector<SetlistEntry>& all() const { return setlists; }
    int size() const { return static_cast<int> (setlists.size()); }
    const SetlistEntry* find (const juce::String& id) const;

    // Create a setlist with a fresh id. Returns the new id, or empty if the
    // name is blank.
    juce::String create (const juce::String& name);
    bool rename (const juce::String& id, const juce::String& name);
    bool remove (const juce::String& id);

    // Membership (idempotent). reposition appends when the snippet is new.
    bool addSnippet (const juce::String& id, int snippetId);
    bool removeSnippet (const juce::String& id, int snippetId);

    // Replace the order with `ids`, keeping only ids already in the setlist.
    bool setOrder (const juce::String& id, const std::vector<int>& ids);

    // Drop snippet ids not present in `validIds` (deleted snippets / a
    // reloaded library folder). Returns true if anything changed.
    bool prune (const std::vector<int>& validIds);

    juce::String toJson() const;
    void fromJson (const juce::String& json);

private:
    std::vector<SetlistEntry> setlists;
    int nextId = 1;
};
