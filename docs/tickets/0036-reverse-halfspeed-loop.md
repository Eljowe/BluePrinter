---
id: "0036"
title: "Reverse and half-speed loop playback"
status: in-progress
blocked_by: []
---

# Reverse and half-speed loop playback

## Problem

The looper only plays forward at 1×. Reverse and half-speed are staples for building
parts and doubling guitar lines, and they are playback-side transforms that do not
touch the recorded audio.

## Current behaviour

`LoopPlayback::render` is a pure forward stepper (position advances one sample per
output sample and wraps to 0 at the window end), shared by the looper and the take
recorder's overdub monitor. Overdub, crop and the playhead all assume forward 1×.

## Decisions (grilled)

- **Scope:** playback-only, over the cropped loop window. Captures stay forward. The
  shared `render` helper gains optional direction/rate parameters that default to
  today's forward 1×, so the take-overdub monitor is not regressed.
- **Controls:** two independent toggles — **Direction** (forward/reverse) × **Half-speed**
  (on/off) — giving four combinations.
- **Half-speed pitch:** tape-style (tempo *and* pitch drop an octave). Pitch-preserved
  time-stretch is out of scope.
- **Overdub:** only runs in forward 1×; turning on overdub resets playback to forward 1×
  and reverse/half are ignored while overdubbing. Stated in the UI.
- **Persistence:** session-only, defaulting to forward 1× each launch (a loop resuming
  in reverse would be surprising; unlike Loop level, these are momentary choices).
- **Non-negotiables:** loop length/grid semantics unchanged and render stays
  length-preserving; half-speed uses linear interpolation (not sample-and-hold); reverse
  one-shot plays end→start then stops (half-speed one-shot takes twice as long); the
  cycle-seam de-click applies in every mode; `loopPlayMeter` keeps metering post-gain; no
  allocation on the audio thread.

## Agent Brief

**Category:** enhancement
**Summary:** Add reverse and tape-style half-speed as playback modes for the loop,
without changing the recorded audio or the shared overdub-monitor path.

**Current behavior:**
Loop playback walks the cropped window forward one sample per output sample. There is
no direction or rate control, and the same render helper drives the take-overdub
monitor.

**Desired behavior:**
- Two independent toggles select direction (forward/reverse) and rate (1× / half-speed).
  All four combinations play correctly and loop seamlessly.
- Reverse reads the cropped window from its end back to its start and wraps cleanly.
- Half-speed is tape-style: it plays at half rate and drops an octave, using linear
  interpolation; loop length and grid semantics are unchanged.
- In one-shot mode, reverse plays to the window start and stops; half-speed one-shot
  simply takes twice as long.
- Overdub is only available in forward 1×: starting an overdub resets playback to
  forward 1×, and reverse/half-speed have no effect while overdubbing.
- The cycle-seam de-click applies in every mode and the playback meter still reflects
  the post-gain output.
- Direction/rate reset to forward 1× on launch (session-only) and never alter the loop
  audio, crop or length.
- The take-overdub monitor path is unchanged (still forward 1×).

**Key interfaces:**
- `LoopPlayback::render` (or an equivalent pure helper) gains direction and rate inputs
  with defaults preserving current behaviour, keeping its length/loop contract.
- The looper owns session-only direction / half-speed flags read on the audio thread and
  passed to the playback tap; overdub start forces them back to forward 1×.
- The transport snapshot carries the current mode so the UI can render it; a new
  frontend→backend event sets direction and half-speed, registered in the shared event
  constants and mirrored in the WebUI bridge (parity test must pass).
- The playhead rendering reflects direction and rate.

**Acceptance criteria:**
- [ ] Reverse plays the loop backwards, wraps cleanly and stays in time over many cycles.
- [ ] Half-speed plays at half rate with the expected octave-down pitch and no artefacts
      beyond linear interpolation.
- [ ] All four direction × rate combinations work, including one-shot reverse stopping at
      the window start.
- [ ] Starting an overdub forces forward 1× and the layer still aligns to the downbeat.
- [ ] Loop length, crop and grid behaviour are unchanged; the take-overdub monitor is
      byte-for-byte unchanged.
- [ ] The seam de-click holds in every mode and the playback meter remains correct.
- [ ] No allocations or locks are added to the audio callback.

## Docs

`AGENTS.md` (looper), `README.md`, `juce-audio` skill.

## Files

`Source/LoopPlayback.h`, `Source/PluginProcessor.h/.cpp`,
`Source/WebViewEditor.h/.cpp`, `WebUI/src/components/Looper.jsx`,
`WebUI/src/bridge.js`, `Tests/`.

## Out of scope

Pitch-preserved (time-stretched) half-speed; arbitrary rate control; reverse capture;
reverse overdub.
