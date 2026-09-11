---
id: "0023"
title: "Take recorder overdub / punch-in"
status: in-progress
blocked_by: []
---

# Take recorder overdub / punch-in

## Decisions

Resolved 2026-09-12: **layered overdub, like the looper.** With a pending take,
a **Dub** toggle in the Take review makes the next record layer the new input over
the take in place (the take keeps playing back while you play along), reusing the
looper's `mixOverdubLayer` wrap-mix and the record-bus tap. Punch-in over a region
is out of scope; a plain manual start/stop drives the layer.

## Problem

The looper can layer new audio over an existing loop (overdub), but the take
recorder always replaces the pending take. There is no way to build a take in
passes (play the rhythm, then layer a lead).

## Current behaviour

- A take capture invalidates any pending take.
- Stop leaves a pending take in `recordBuffer`; `savePendingTake` /
  `discardPendingTake` handle it.
- The looper's `mixOverdubLayer` wrap-mixes a captured layer into the loop under
  `recordLock`; that is the model reused here.

## Plan

1. **State**: add `takeOverdub` (session-only toggle), `takeOverdubCapture` (true
   while the layer is being captured), `takeOverdubPending` (a count-in is
   leading into an overdub) and `takeOverdubPlayPos`. The layer reuses
   `recordWritePos`, based at `takeLength` (the existing take's end).
2. **Start/stop**: `startRecording` computes `overdubbing = takeOverdub &&
   takePending && takeLength > 0`. Without a count-in it calls
   `beginActualTakeOverdub()`; with one, the pre-roll transition does.
   `beginActualTakeOverdub` keeps the take audio and pending state, arms the
   layer write at `takeLength` and starts the take playback from position 0.
3. **Audio path**: while `takeOverdubCapture`, the monitor mix plays the pending
   take additively and looping (monitor-only — never added to
   `recordingMixBuffer`, so the layer holds only the new playing). The record tap
   writes the layer with the existing `writeRecording`.
4. **Finalize**: `finalizeRecordingOnMessageThread` branches on
   `takeOverdubCapture`; the layer `[takeLength, recordWritePos)` is wrap-mixed
   into `[0, takeLength)` with `mixOverdubLayer` (generalised with a `loopStart`
   parameter and a clip-latch target), then the take peaks refresh. The take
   length is unchanged.
5. **UI**: a Dub toggle in `TakeReview.jsx` (only while a take is pending),
   shipped as `takeOverdub` in the transport snapshot; the Transport status pill
   reads "overdub" during a layered capture. New event `frontendSetTakeOverdub`.

## Acceptance criteria

- With a pending take and Dub on, recording captures only the new input; on stop
  it is mixed into the take and the take plays back layered.
- Dub off (or no pending take) records a fresh take exactly as before.
- A count-in keeps the take silent until capture begins, then starts it from the
  downbeat so the layer aligns.
- Saving/discarding the layered take behaves as before (single WAV, layers baked
  in).
- `takeOverdub` is session-only (not persisted), like the looper's.

## Docs

`README.md` (Recording model), `AGENTS.md` (Recording / looper overdub).

## Files

`Source/PluginProcessor.h/.cpp`, `Source/WebViewEditor.h/.cpp`,
`WebUI/src/components/TakeReview.jsx`, `WebUI/src/components/Transport.jsx`,
`WebUI/src/bridge.js`.

## Out of scope

Full comping/takes-stacking UI; MIDI take recording.
