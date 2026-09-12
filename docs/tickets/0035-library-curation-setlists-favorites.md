---
id: "0035"
title: "Library curation: setlists, favourites and bulk edits"
status: needs-triage
blocked_by: []
---

# Library curation: setlists, favourites and bulk edits

## Problem

The library is a flat, single-item grid with colour tags. There is no way to group
takes into a set for a performance, mark favourites to prioritise them, or act on
several takes at once. As the library grows this becomes the main friction.

## Current behaviour

`SnippetList.jsx` offers frontend-only search/sort/key-filter/tag-chip filtering and
caps the grid at 10 cards + "Show more". `SnippetCard.jsx` operations (rename,
colour, gain, export, reveal) are per-item only. Snippets persist WAV + JSON in the
library folder.

## Required change

1. **Favourites/rating** on a snippet, persisted in the JSON sidecar and shipped in
   the snapshot, plus a filter/toggle in the toolbar.
2. **Named setlists** (ordered lists of snippet ids) stored in the properties file:
   create/rename/reorder/add/remove, with a setlist view or filter.
3. **Multi-select** in the grid with bulk actions: set tag colour, add to setlist,
   export, delete (with confirmation).
4. Keep filtering/sorting frontend-only; do not move search into C++.
5. Define behaviour when a setlist entry is deleted or its file is missing.

## Acceptance criteria

- Rating/favourite survives restart and a sidecar export round-trip.
- Setlists persist across restart and reordering is stable.
- Bulk tag / delete / add-to-setlist act on the whole selection, with confirmation or
  undo for destructive actions.
- The grid stays responsive on a large library.

## Docs

`AGENTS.md` (library/snippets), `README.md`, `webui-bridge` skill.

## Files

`Source/SnippetLibrary.h/.cpp`, `Source/PluginProcessor.h/.cpp` (user state),
`Source/WebViewEditor.h/.cpp`, `WebUI/src/components/SnippetList.jsx`,
`WebUI/src/components/SnippetCard.jsx`, `WebUI/src/bridge.js`.

## Out of scope

Cloud sync; auto-generated smart playlists; drag-and-drop between setlists.
