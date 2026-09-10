---
id: "0008"
title: "Overdub capture with a start-cropped loop writes the layer over the loop tail"
status: done
blocked_by: []
---

# Overdub capture with a start-cropped loop writes the layer over the loop tail

## Problem

When overdubbing onto a loop that has been cropped at the start, the overdub layer
is written into memory that still belongs to the loop window, corrupting the loop's
tail (and the subsequent wrap-mix).

## Current behaviour

- `setLooperRecording` sets `overdubWritePos = audioLoopLength` (the CROPPED length,
  `PluginProcessor.cpp:1517`), ignoring `audioLoopStart`.
- The capture tap writes the layer at `[audioLoopLength, audioLoopLength + layerLength)`
  (`:804-829`).
- The loop window is `[audioLoopStart, audioLoopStart + audioLoopLength)`.
- With a start crop (`audioLoopStart > 0`), the layer region starts *before* the loop
  window ends, so the two overlap by `audioLoopStart` samples: the layer overwrites the
  loop's tail.
- `mixOverdubLayer` reads the layer from the same base (`loopLength + offset`, `:1651-1660`)
  and mixes it into `[audioLoopStart + cyclePos]`, so it processes the corrupted region.

End-crop-only is fine (the window ends at `audioLoopLength`); the bug needs a start
crop.

## Required change

- Write the layer after the FULL loop, i.e. at base `audioLoopFullLength` (always
  outside the cropped window), and clamp against `maxRecordSamples` as today.
- Pass that base to `mixOverdubLayer` separately from the window length: it still uses
  `audioLoopLength` for the wrapping cycle math and `audioLoopStart` for the target,
  but reads the layer from the new base. Compute `layerLength` from the actual write
  end minus the base (not `overdubWritePos - audioLoopLength`).

## Acceptance criteria

- Overdub onto a start-cropped loop leaves the loop's existing audio intact.
- A one-loop-cycle layer mixes 1:1; start/end/both crops all work.
- No layer region overlaps the loop window (or the pre-crop / post-crop regions).

## Files

`Source/PluginProcessor.cpp` (`setLooperRecording`, `processBlock` step 3,
`mixOverdubLayer`).

## Out of scope

Count-in alignment (0006).
