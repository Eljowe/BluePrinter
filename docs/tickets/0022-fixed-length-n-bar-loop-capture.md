---
id: "0022"
title: "Fixed-length N-bar loop capture"
status: ready-for-agent
blocked_by: []
---

# Fixed-length N-bar loop capture

## Problem

The looper stops only when the user stops it, then trims the capture to the
nearest full bar. There is no way to record exactly N bars, so loops can come back
with an unintended length and the trim can feel arbitrary. A performer who knows
the loop is 4 bars wants the capture to end itself on the grid.

## Current behaviour

`setLooperRecording` captures freely into `recordBuffer`; on stop,
`trimLooperToMusicalGrid` snaps the window to the nearest bar length (zero-padding
short captures, truncating long ones). Crop start/end applies afterwards in whole
beats.

## Required change

1. Add a **Length** mode to the looper: `Free` (current behaviour) or `N bars`
   (1 / 2 / 4 / 8, or a stepper).
2. In fixed mode, auto-stop the capture after exactly N bars at the current BPM,
   driven off the same metronome/clock position that the count-in uses. No drift:
   start on the grid and stop exactly N bars later.
3. Keep count-in pre-roll as-is; the fixed length applies to the capture, not the
   count-in.
4. UI: add the length selector to the looper settings strip; show the target length
   while capturing.
5. Reuse the existing bar math rather than adding a parallel timing path.

## Acceptance criteria

- Fixed mode captures exactly N bars (verified over many cycles with the click).
- The loop stays in time over repeated playback cycles.
- Free mode is unchanged.
- Crop still applies after capture and cannot corrupt the fixed length.
- The chosen length persists with the other looper state.

## Docs

`README.md` (Audio looper), `AGENTS.md` (looper/capture), `juce-audio` and
`webui-bridge` skill notes.

## Files

`Source/PluginProcessor.h/.cpp` (looper capture/stop + snapshot),
`Source/WebViewEditor.h/.cpp`, `WebUI/src/components/Looper.jsx`,
`WebUI/src/bridge.js`.

## Out of scope

Take-recorder fixed-length capture; tempo detection of an existing loop.
