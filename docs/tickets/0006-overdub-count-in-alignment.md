---
id: "0006"
title: "Looper overdub count-in misaligns the layer with the loop downbeat"
status: done
blocked_by: []
---

# Looper overdub count-in misaligns the layer with the loop downbeat

## Problem

With **Overdub** on and a looper **count-in**, the loop plays and advances during the
count-in. When capture is armed at the end of the count-in, `audioLoopPosition` is
mid-cycle, so the recorded layer's offset 0 no longer corresponds to the loop's
downbeat. `mixOverdubLayer` maps layer offset 0 to loop cycle position 0, so the
layered take lands in the middle of the previously recorded part. The count-in should
lead into the start of the loop: capture must begin at loop position 0.

## Current behaviour

- `setLooperRecording` overdub branch starts the loop before the count-in:
  `audioLoopPosition = 0`, `audioLoopPlaying = true` (`PluginProcessor.cpp:1504-1521`),
  then arms the pre-roll (`:1540-1549`).
- `processBlock` step 6 advances `audioLoopPosition` through the pre-roll (`:842-896`).
- Step 8 flips `looperCaptureArmed` when the count-in completes but never resets
  `audioLoopPosition` (`:916-950`).
- The capture tap writes the layer from `overdubWritePos` (`:804-829`); `mixOverdubLayer`
  wraps layer offset 0 onto loop `cyclePos 0` (`:1642-1662`).

## Required change

Make an overdub capture begin at loop position 0 so the layer aligns with the loop
downbeat. During an overdub count-in, do **not** run the loop (`audioLoopPlaying =
false` through the pre-roll), then at the count-in -> capture transition set
`audioLoopPlaying = true` and `audioLoopPosition = 0` in the same step that arms
capture (step 8). The count-in therefore plays the click over silence and the loop
starts exactly at the downbeat when the layer capture begins.

Fresh captures and count-in = 0 are unaffected (both already begin at position 0).

## Acceptance criteria

- With Overdub + count-in, after the count-in the loop and the new layer start together
  at the loop downbeat.
- A one-loop-cycle layer mixes 1:1 onto the loop; stopping and replaying the overdub
  lines up rhythmically.
- Non-overdub count-in and count-in = 0 behaviour unchanged.

## Docs

Note the overdub count-in alignment rule in `AGENTS.md` (looper/overdub) and the
`webui-bridge` skill if it describes the overdub flow.

## Files

`Source/PluginProcessor.cpp` (`setLooperRecording`, `processBlock` steps 3/6/8).

## Out of scope

Fresh-capture count-in; take-recorder count-in.
