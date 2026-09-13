---
id: "0052"
title: "Tempo-locked (time-stretched) loop playback"
status: needs-triage
blocked_by: []
---

# Tempo-locked (time-stretched) loop playback

## Problem

Loop and snippet playback run at their recorded rate. The reverse/half-speed mode
is tape-style (`rate` + linear interpolation), which changes pitch, so a loop
cannot follow the session BPM or a newly imported backing track without
re-recording.

## Current behaviour

- `LoopPlayback::PlaybackMode` has `reverse` + `rate` (0.5 = tape-style
  half-speed), documented as shared by the looper and the take-overdub monitor.
- No pitch-preserving time-stretch exists.

## Open questions (resolve before implementation)

1. Algorithm: WSOLA / phase vocoder / a lightweight library? CPU budget on the
   audio thread and added latency?
2. Scope: loop playback only, or also library snippet playback and
   bounce/export?
3. Quality vs latency trade-off; window size; artefact tolerance.
4. Interaction with reverse, seam de-click, crop, and overdub (overdub must stay
   1× and un-stretched).
5. Persist "follow session tempo" per loop/snippet or globally?
6. Better split into a research spike (measure an algorithm) before a full
   ticket?

## Required change (proposed, pending answers)

- A pure time-stretch module (unit-tested on synthetic tones) and a
  `PlaybackMode` extension for a stretch ratio.
- A UI toggle to follow the session BPM.

## Acceptance criteria

TBD once the open questions are answered.

## Docs

`AGENTS.md` (loop playback), `juce-audio` skill, possibly an ADR on the
algorithm.

## Files

New `Source/TimeStretch.h/.cpp` + `Tests/`, `Source/LoopPlayback.h`,
`Source/PluginProcessor.*`, `WebUI/src/components/Looper.jsx`,
`WebUI/src/bridge.js`.

## Out of scope

Real-time pitch shifting with formant control; offline render quality.
