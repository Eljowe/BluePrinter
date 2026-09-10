---
id: "0001"
title: "Rework the Monitor section: Input/Output in dB, remove Dry, add Loop level"
status: done
blocked_by: []
---

# Rework the Monitor section: Input/Output in dB, remove Dry, add Loop level

## Problem

The Monitor deck exposes `Gain` (0..1 linear), `Dry` (0..1) and `Play Vol` (0..1),
which conflate the capture level with the monitoring level. In the looper, captured
audio replays at a fixed level with no way to lower it without also lowering the new
live layer, so overdubbing over a loud loop is nearly impossible.

## Current behaviour

- `Gain` (APVTS, 0..1) trims the live input at the top of `processBlock`
  (`PluginProcessor.cpp:678-680`) and therefore affects both the monitor and the capture.
- `Dry` (`dryLevel` atomic) scales the direct dry path in the monitor and the record
  mix (`:705-709`, `:731`).
- `PlaybackVolume` (APVTS) scales only snippet/take playback (`renderPlayback`,
  `renderTakePlayback`); loop playback has no level control (`:852-905`).

## Required change

1. **Input** = the existing `Gain` param, relabelled and moved to a dB range
   (`-12..+24`, default 0). Applied as `Decibels::decibelsToGain` at the top of
   `processBlock`.
2. **Output** = the existing `PlaybackVolume` param, relabelled "Output" and moved to
   a dB range (`-60..+12`, default 0). Applied once as master monitor gain at the END
   of `processBlock`, after the metronome/MIDI clock block. It must NOT affect the
   recording capture (`recordingMixBuffer`) or the looper capture tap. Remove the
   local `playbackVol` scaling in `renderPlayback`/`renderTakePlayback`.
3. **Loop level** = new atomic (dB, `-60..+12`, default 0), monitor-only gain on loop
   playback (`:852-905`, including the crossfade head). Persisted in the state blob
   next to the other non-automatable state. New `frontendSetLoopLevel` event and a
   knob in the Looper panel.
4. **Remove Dry** entirely (`dryLevel` atomic, getter/setter, `frontendSetDryLevel`,
   transport snapshot field, `App.jsx`/`HeaderControls.jsx`/`bridge.js` references,
   `getStateInformation`/`setStateInformation` properties). The dry pass-through
   becomes unity.
   > Superseded by ticket 0010: the Dry control is back as a global dB level
   > (−60..0) that scales the dry in both the monitor and the capture.
5. Keep the Input meter source unchanged (computed at `:842`) so it reflects the
   record level, not the master Output.

## Acceptance criteria

- Input changes both the monitor and the captured audio.
- Output changes only what is heard; a take recorded with Output at minimum is unchanged.
- Loop level changes loop playback only; overdub layers and the saved loop are unchanged.
- Dry no longer exists; no dead references.
- Input/Output knobs show dB.

## Docs

Update `AGENTS.md` and the `juce-audio`/`webui-bridge` skill notes listing the APVTS
params/events.

## Files

`Source/PluginProcessor.h/.cpp`, `Source/WebViewEditor.h/.cpp`,
`WebUI/src/bridge.js`, `WebUI/src/App.jsx`,
`WebUI/src/components/HeaderControls.jsx`, `WebUI/src/components/Looper.jsx`,
`WebUI/src/styles.css`.

## Out of scope

Metering/clip (0002), chain solo/mute (0003), snippet gain (0004), overdub guard (0005).
