---
id: "0002"
title: "Add record/output metering, dB meter scale, and clip latches"
status: done
blocked_by: ["0001"]
---

# Add record/output metering, dB meter scale, and clip latches

## Problem

Only an input meter exists (`inputLevel`/`inputPeak`, computed pre-loop/click at
`PluginProcessor.cpp:842`). Nothing shows the capture level or the master output, and
no meter detects clipping. Takes aren't auto-saved, so a clipped print is
unrecoverable. `LevelMeter.jsx` is a linear 0..1 bar with two unlabeled ticks.

## Required change

### Backend

- Keep `inputLevel`/`inputPeak` as the live monitor meter.
- Add `recordLevel`/`recordPeak` computed from `recordingMixBuffer` after the chain
  loop (the actual print).
- Add `outputLevel`/`outputPeak` computed from `buffer` after the master Output gain
  at the end of `processBlock`.
- Add a loop playback peak during loop playback.
- Add latched clip flags `inputClipped`/`recordClipped`/`outputClipped`/`loopClipped`;
  extend `computeLevelsInto` to set a clip flag when peak >= 1.0. Decay
  output/record/loop peaks in the 30 Hz timer alongside `inputPeak` (`:3186-3189`).
- Emit all new fields in `makeTransportSnapshot`.
- New `frontendResetClip` `{ target }` event to clear a latch (or all).

### Frontend

- Rework `LevelMeter.jsx`: linear→dB mapping (`-60..0`), headroom zone, persistent
  peak-hold marker, latched red clip state with click-to-reset, and a `label` prop
  (the aria-label is currently hardcoded "Input level").
- Monitor deck shows IN / REC / OUT meters (each latched) beside the Input/Output knobs.
- Looper shows a loop clip indicator.

## Acceptance criteria

- REC meter reflects what will be printed and is unaffected by Output.
- OUT meter reflects the post-Output signal.
- Clip LEDs latch until clicked.
- Meters use a dB scale.

## Docs

Update `AGENTS.md` + the `webui-bridge` skill (new transport fields, clip events).

## Files

`Source/PluginProcessor.h/.cpp`, `Source/WebViewEditor.cpp`,
`WebUI/src/components/LevelMeter.jsx`, `WebUI/src/App.jsx`, `WebUI/src/bridge.js`,
`WebUI/src/styles.css`.

## Out of scope

The Input/Output routing itself (0001); chain or snippet metering.
