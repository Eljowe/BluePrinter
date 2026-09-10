---
id: "0004"
title: "Per-snippet playback gain + normalize"
status: done
blocked_by: []
---

# Per-snippet playback gain + normalize

## Problem

Saved snippets play back at their captured level. Takes recorded at different Input
levels play back at wildly different loudness with no trim, so the library can't be
balanced.

## Required change

- Add `float gainDb = 0.0f` to `Snippet` (`SnippetLibrary.h`), persisted as
  `"gainDb"` in the JSON sidecar (`SnippetLibrary.cpp:413-440`, read near `:308`).
- Add `updateSnippetGain(id, db)` (mirrors `updateColor`) persisted via
  `persistMetadata`.
- `renderPlayback` applies `Decibels::decibelsToGain(snippet.gainDb)` before the
  master Output.
- `snippetToVar` emits `gainDb`.
- Events `frontendSetSnippetGain` `{ id, gainDb }` and `frontendNormalizeSnippet`
  `{ id }` (sets gain so the peak lands at -1 dBFS).
- `SnippetCard` expanded details gets a Gain knob (`-24..+24 dB`) and a Normalize
  button.

## Acceptance criteria

- Gain is non-destructive (sidecar only) and survives reload.
- Normalize brings the snippet peak to ~-1 dBFS.
- Two snippets at different captured levels can be balanced.

## Docs

Update `AGENTS.md` (snippet model) + the `webui-bridge` skill.

## Files

`Source/SnippetLibrary.h/.cpp`, `Source/PluginProcessor.h/.cpp`,
`Source/WebViewEditor.h/.cpp`, `WebUI/src/bridge.js`, `WebUI/src/App.jsx`,
`WebUI/src/components/SnippetCard.jsx`, `WebUI/src/styles.css`.

## Out of scope

Baking gain into the saved WAV; per-tag or global library gain.
