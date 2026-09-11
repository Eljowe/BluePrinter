---
id: "0025"
title: "Export audio in multiple formats and bounce a loop to the library"
status: in-progress
blocked_by: []
---

# Audio export in multiple formats and bounce a loop to the library

## Problem

Snippets save only as 16-bit WAV through the existing save path. Users who want a
compressed or lossless alternative, or who want the current looper/chain output as
a library entry, have no route to it.

## Current behaviour

`SnippetLibrary::saveSnippetToFolder` writes WAV + JSON; `saveLoopSnippet` converts
the captured loop into a library snippet and writes it to the folder. The
non-destructive `Snippet.gainDb` trim is applied on playback, never baked into the
saved WAV.

## Required change

1. Add **Export as…** with a format choice (WAV, FLAC, AIFF at minimum) using
   `juce::AudioFormatManager` / the matching `AudioFormatWriter`.
2. Decide and document whether the exported file applies `Snippet.gainDb`:
   recommended **yes for export** (the point is a finished file) while the library
   save remains non-destructive — make the distinction explicit in the UI.
3. Add a **Bounce loop to snippet** action that materialises the current cropped
   loop as a new library entry (reusing `saveLoopSnippet`), so it can be renamed,
   tagged, and exported like any other snippet.
4. Notify on success/failure with the output path.

## Acceptance criteria

- Exported files open in a standard player and match the monitored level.
- The format list reflects what the JUCE build actually supports.
- Bouncing the loop creates a normal library snippet that survives a restart.
- Failure (locked file, unsupported settings) produces a clear notification.

## Docs

`README.md` (save/export), `AGENTS.md` (snippet gain semantics),
`webui-bridge` skill.

## Files

`Source/SnippetLibrary.h/.cpp`, `Source/PluginProcessor.h/.cpp`,
`Source/WebViewEditor.h/.cpp`, `WebUI/src/components/SnippetCard.jsx`,
`WebUI/src/components/Looper.jsx`, `WebUI/src/bridge.js`.

## Out of scope

Streaming/batch export of the whole library; cloud upload.
