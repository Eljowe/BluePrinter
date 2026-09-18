---
id: "0057"
title: "Seek snippet playback from the library waveform"
status: done
blocked_by: []
---

# Seek snippet playback from the library waveform

## Problem

A library snippet always plays from sample 0. There is no way to audition a
specific phrase, so working on the middle of a long take means waiting for it
to reach that point (or exporting and using an external player).

## Current behaviour

`startPlayback(snippetId)` resets `playbackReadPos` to 0 and `renderPlayback`
reads forward from there; the `SnippetCard` waveform is display-only (a
playhead is drawn while playing, but the surface takes no pointer input).

## Required change

- `startPlayback(snippetId, startSample = 0)` starts partway in (audio thread
  clamps).
- New `frontendSetPlaybackPosition { position }` jumps the currently-playing
  snippet (`PluginProcessor::setPlaybackPosition` sets an atomic pending seek
  that `renderPlayback` applies after the snippet re-fetch, so the re-fetch's
  reset to 0 cannot clobber it).
- `SnippetCard`'s expanded waveform is a `role="slider"`: click starts playback
  from that point, pointer-drag scrubs, arrow/PageUp/Down/Home/End seek by
  ~1 s / 5 s / ends. The sample offset derives from the pointer x within the
  playhead's 6px-inset track.

## Acceptance criteria

- Clicking a snippet waveform starts playback at the clicked point; dragging
  while it plays moves the cursor (scrub).
- Starting a snippet normally still begins at 0.
- Bridge parity (`Tests/check-bridge-events.mjs`) is green.

## Docs

`AGENTS.md` (snippets), webui-bridge skill.

## Files

`Source/PluginProcessor.h/.cpp`, `Source/WebViewEditor.h/.cpp`,
`WebUI/src/bridge.js`, `WebUI/src/components/SnippetCard.jsx`,
`WebUI/src/styles.css`.

## Out of scope

- Seeking the take-review timeline or the looper (library cards only, per the
  request). The looper already has its own crop/zoom surface.

## Comments

2026-09-18 — Implemented. `Tests/check-bridge-events.mjs` green (105 frontend
events); WebUI `npm run build` green. The runtime click/drag check is a manual
step on Windows.
