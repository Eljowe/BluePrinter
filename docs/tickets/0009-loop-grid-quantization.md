---
id: "0009"
title: "Looper: non-grid loop length makes the downbeat drift (second cycle starts late)"
status: done
blocked_by: []
---

# Looper: non-grid loop length makes the downbeat drift (second cycle starts late)

## Problem

A captured loop is supposed to be trimmed to a whole number of bars so its
downbeat stays on the beat grid on every cycle. When the capture stopped a
little short of a bar boundary, the trim clamped the snapped length back to the
raw capture instead of padding it, so the loop kept a non-grid length. At
120 BPM a nominally 4-bar loop that came in ~882 samples (20 ms) short still
wrapped 20 ms early, and a capture in the upper half of a bar (e.g. 3.6 bars
intended as 4) could wrap most of a bar late — heard as "the second loop starts
about a second later than it should", and getting worse every cycle relative to
the click.

This survived the 0007 seam-declck fix because 0007 only changed *how* the loop
wraps, not the length of the window being wrapped.

## Current behaviour

`trimLooperToMusicalGrid` (`Source/PluginProcessor.cpp`) computed

```cpp
auto target = llround (captured / bar) * bar;
target = juce::jlimit<int64_t> (1, captured, target);
```

`jlimit(1, captured, target)` clamps the snapped target down to the raw capture,
so a short capture is never padded and a loop is never guaranteed to be a whole
number of bars. Playback then wraps at that non-grid `audioLoopLength`, so the
downbeat lands off the click grid and drifts.

## Required change

Make the loop exactly the snapped length:

- Round the capture to the nearest whole bar (beat fallback for sub-half-bar
  captures).
- Clamp the target only to the record buffer capacity, not to `captured`.
- When the target is longer than the capture, zero-fill `[captured, target)` so
  the padded tail is silence (the record buffer may still hold a previous take's
  audio there). Audio past the target is truncated by the length itself.

## Acceptance criteria

- A captured loop's `audioLoopLength` / `audioLoopFullLength` is an integer
  number of bars (or beats for sub-half-bar captures).
- A capture stopped a little short of a bar boundary wraps exactly on the grid;
  the second and later cycles line up with the click.
- A capture stopped a little past a bar boundary is truncated to the grid.
- The padded tail is silent (no bleed from a previous take).
- Crop start/end still derive from `audioLoopFullLength` and stay reversible.

## Docs

Note the grid-quantization rule in `AGENTS.md` (looper) and the `juce-audio`
skill's audio-path step for loop capture/trim.

## Files

`Source/PluginProcessor.cpp` (`trimLooperToMusicalGrid`).

## Out of scope

Seam declick (0007); overdub crop layer placement (0008).
