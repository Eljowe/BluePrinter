---
id: "0023"
title: "Take recorder overdub / punch-in"
status: needs-info
blocked_by: []
---

# Take recorder overdub / punch-in

## Problem

The looper can layer new audio over an existing loop (overdub), but the take
recorder always replaces the pending take. There is no way to build a take in
passes (play the rhythm, then layer a lead) or to punch in over a mistake. The
scope is not yet defined, so this ticket is not ready for an agent.

## Current behaviour

- A take capture invalidates any pending take.
- Stop leaves a pending take in `recordBuffer`; `savePendingTake` /
  `discardPendingTake` handle it.
- The looper's `mixOverdubLayer` wrap-mixes a captured layer into the loop under
  `recordLock`; that is the closest existing model, but takes have no loop or bar
  grid to align to.

## Open questions (resolve before implementation)

1. Layer **onto the pending take in place** (like the looper), or record a new
   layer that is non-destructively combined at playback?
2. Punch-in/out boundaries: manual start/stop, a bar count, or an existing
   snippet/loop region?
3. Alignment: a take has no fixed grid; does overdub require the metronome/bar
   reference (making the take effectively loop-length), or is sample-accurate
   free overdub acceptable?
4. Does saving a layered take bake the layers into one WAV (and a sidecar listing
   the layers), or keep layers separate?

## Required change (proposed, pending answers)

Likely reuse `mixOverdubLayer` and the record-bus tap, with a distinct
`takeOverdub` state and UI (a Dub toggle in the Take review, mirroring the
looper).

## Acceptance criteria

TBD once the open questions are answered.

## Docs

`README.md` (Recording model), `AGENTS.md` (Recording / looper overdub).

## Files

`Source/PluginProcessor.h/.cpp`, `Source/WebViewEditor.h/.cpp`,
`WebUI/src/components/TakeReview.jsx`, `WebUI/src/components/Transport.jsx`,
`WebUI/src/bridge.js`.

## Out of scope

Full comping/takes-stacking UI; MIDI take recording.
