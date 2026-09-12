---
id: "0038"
title: "Stem export: render each chain separately"
status: needs-triage
blocked_by: []
---

# Stem export: render each chain separately

## Problem

Export bounces only the final mix. To mix the parts in a DAW the user has to solo
chains and re-export one at a time, and there is no guaranteed alignment between the
resulting files.

## Current behaviour

`SnippetLibrary::exportSnippetToFile` writes the snippet (baking `gainDb`) and
`saveLoopSnippet` materialises the loop mix. Chains run in parallel into a summed
monitor/capture mix; there is no per-chain render output.

## Required change

1. Render each chain's output (post-volume, respecting its mute) to its own file using
   the existing offline render path.
2. Optionally render the dry input as its own stem.
3. Name files `<snippet-or-loop>-<chain>.<ext>`; all stems must start at the same
   sample so they align in a DAW.
4. Define whether stems follow `recordOnCapture` or the monitor state, and document it.
5. Reuse the 0025 format chooser (WAV/AIFF/FLAC) and its gain-bake semantics.
6. Offline render must not run on the audio thread; reuse the `renderPlayback`-style
   path.

## Acceptance criteria

- Stems sum back to the exported mix within a small tolerance under the documented
  dry policy.
- Each stem is correctly named and format-correct, with identical lengths/start.
- Muted and monitor-only chains are handled per the documented rules.
- A multi-stem export reports per-file success/failure.

## Docs

`AGENTS.md` (export, chain routing), `README.md`.

## Files

`Source/PluginProcessor.h/.cpp` (offline render paths), `Source/SnippetLibrary.h/.cpp`,
`Source/WebViewEditor.h/.cpp`, `WebUI/src/components/SnippetCard.jsx` /
`WebUI/src/components/Looper.jsx`, `WebUI/src/bridge.js`.

## Out of scope

DAW stem packages/metadata; batch export of the whole library.
