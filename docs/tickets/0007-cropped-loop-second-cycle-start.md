---
id: "0007"
title: "Cropped looper: second cycle sounds like it restarts late (wrap crossfade pre-plays the loop head)"
status: done
blocked_by: []
---

# Cropped looper: second cycle sounds like it restarts late (wrap crossfade pre-plays the loop head)

## Problem

With a **cropped** loop, on the second and later cycles the loop sounds like it
starts a bit after the crop start. The wrap crossfade blended the loop's head (the
crop start) into its tail, so the head was heard at the END of the previous cycle;
the first `fadeLen` samples of the new cycle were then a repeat, and the "fresh"
audio effectively began at `cropStart + fadeLen` (~3 ms). The crossfade also read
`tail` from the live-mixed output buffer (which already contains live input +
chains), so `(head - tail)` ducked the direct monitor at every seam.

## Current behaviour

`processBlock` step 6 (loop playback, `PluginProcessor.cpp:842-896`) added the loop
with `addFrom`, then on the wrap blended `head = recordBuffer[start + k]` into
`tail = buffer.getSample(...)` and set `position = 0`. Because the head was injected
at the tail and then replayed at position 0, the crop start was heard twice and the
cycle's fresh content lagged by `fadeLen`.

## Fix (implemented)

Replaced the run-based playback with a per-sample loop that adds only the loop's own
contribution on top of the live mix, with a short symmetrical fade in/out at the seam
(`declick = min(loopCrossfadeSamples, length / 2)`). No head pre-play and no
read/modify of the live-mixed buffer; the phase wraps to 0, so every cycle starts
exactly at `audioLoopStart`. The seam stays click-free (a ~3 ms fade at both ends).

## Acceptance criteria

- With a cropped loop, every cycle after the first starts at the crop start (no
  ~3 ms lag / flam).
- The direct monitor is not ducked at the seam.
- The wrap does not click.
- One-shot mode still stops at the loop end and leaves the block remainder as
  live-through.

## Docs

Updated the `juce-audio` skill's audio-path step 6 (crossfade -> seam declick).

## Files

`Source/PluginProcessor.cpp` (`processBlock` step 6).

## Out of scope

Overdub + crop layer placement (0008).
