#include "TestRunner.h"
#include "SetlistStore.h"

BP_TEST (SetlistStore_createRenameRemove)
{
    SetlistStore store;
    const auto a = store.create ("Set A");
    BP_CHECK (a.isNotEmpty());
    BP_CHECK_EQ (store.size(), 1);
    BP_CHECK_EQ (store.find (a)->name, juce::String ("Set A"));
    BP_CHECK (store.create ("").isEmpty());          // blank refused
    BP_CHECK (store.create ("   ").isEmpty());

    const auto b = store.create ("Set B");
    BP_CHECK (a != b);

    BP_CHECK (store.rename (a, "Renamed"));
    BP_CHECK_EQ (store.find (a)->name, juce::String ("Renamed"));
    BP_CHECK (! store.rename (a, "  "));             // blank refused
    BP_CHECK (! store.rename ("nope", "x"));

    BP_CHECK (store.remove (b));
    BP_CHECK_EQ (store.size(), 1);
    BP_CHECK (! store.remove (b));
}

BP_TEST (SetlistStore_membershipIsOrderedAndIdempotent)
{
    SetlistStore store;
    const auto id = store.create ("Set");

    BP_CHECK (store.addSnippet (id, 5));
    BP_CHECK (store.addSnippet (id, 3));
    BP_CHECK (store.addSnippet (id, 5));             // duplicate ignored
    BP_CHECK_EQ (store.find (id)->snippetIds.size(), 2);
    BP_CHECK_EQ (store.find (id)->snippetIds[0], 5);
    BP_CHECK_EQ (store.find (id)->snippetIds[1], 3);

    BP_CHECK (store.removeSnippet (id, 5));
    BP_CHECK_EQ (store.find (id)->snippetIds.size(), 1);
    BP_CHECK_EQ (store.find (id)->snippetIds[0], 3);
    BP_CHECK (store.removeSnippet (id, 5));          // no-op still ok

    BP_CHECK (! store.addSnippet ("missing", 1));
    BP_CHECK (! store.removeSnippet ("missing", 1));
}

BP_TEST (SetlistStore_setOrderFiltersAndKeepsMembers)
{
    SetlistStore store;
    const auto id = store.create ("Set");
    store.addSnippet (id, 1);
    store.addSnippet (id, 2);
    store.addSnippet (id, 3);

    // Valid reorder.
    BP_CHECK (store.setOrder (id, { 3, 1, 2 }));
    BP_CHECK_EQ (store.find (id)->snippetIds[0], 3);
    BP_CHECK_EQ (store.find (id)->snippetIds[1], 1);
    BP_CHECK_EQ (store.find (id)->snippetIds[2], 2);

    // Unknown ids dropped; omitted members kept at the end.
    BP_CHECK (store.setOrder (id, { 2, 99, 2 }));
    BP_CHECK_EQ (store.find (id)->snippetIds.size(), 3);
    BP_CHECK_EQ (store.find (id)->snippetIds[0], 2);
    BP_CHECK_EQ (store.find (id)->snippetIds[1], 3);
    BP_CHECK_EQ (store.find (id)->snippetIds[2], 1);

    BP_CHECK (! store.setOrder ("missing", { 1 }));
}

BP_TEST (SetlistStore_pruneDropsDeletedSnippets)
{
    SetlistStore store;
    const auto id = store.create ("Set");
    store.addSnippet (id, 1);
    store.addSnippet (id, 2);
    store.addSnippet (id, 3);

    BP_CHECK (store.prune ({ 1, 3 }));
    BP_CHECK_EQ (store.find (id)->snippetIds.size(), 2);
    BP_CHECK_EQ (store.find (id)->snippetIds[0], 1);
    BP_CHECK_EQ (store.find (id)->snippetIds[1], 3);

    BP_CHECK (! store.prune ({ 1, 3 }));             // idempotent
}

BP_TEST (SetlistStore_jsonRoundTrips)
{
    SetlistStore source;
    const auto a = source.create ("Gig set");
    source.addSnippet (a, 7);
    source.addSnippet (a, 2);
    const auto b = source.create ("Practice");
    source.addSnippet (b, 5);

    SetlistStore loaded;
    loaded.fromJson (source.toJson());
    BP_CHECK_EQ (loaded.size(), 2);
    BP_CHECK_EQ (loaded.find (a)->name, juce::String ("Gig set"));
    BP_CHECK_EQ (loaded.find (a)->snippetIds.size(), 2);
    BP_CHECK_EQ (loaded.find (a)->snippetIds[0], 7);
    BP_CHECK_EQ (loaded.find (b)->name, juce::String ("Practice"));

    // A freshly created id must not collide with a restored one.
    const auto c = loaded.create ("New");
    BP_CHECK (c != a);
    BP_CHECK (c != b);
}

BP_TEST (SetlistStore_jsonTolerantOfGarbage)
{
    SetlistStore store;
    store.fromJson ("not json at all");
    BP_CHECK_EQ (store.size(), 0);

    store.fromJson ("[{\"name\":\"No id\",\"ids\":[1,1,2]},{\"id\":\"9\",\"name\":\"X\"}]");
    BP_CHECK_EQ (store.size(), 2);
    // Missing id gets assigned; duplicate member ids collapse.
    BP_CHECK_EQ (store.all()[0].snippetIds.size(), 2);
    BP_CHECK (store.all()[0].id.isNotEmpty());
    BP_CHECK_EQ (store.all()[1].id, juce::String ("9"));

    // nextId continues past the highest restored id.
    const auto created = store.create ("after");
    BP_CHECK_EQ (created, juce::String ("10"));
}
