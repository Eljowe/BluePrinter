---
id: "0050"
title: "Click subdivisions and accents"
status: needs-triage
blocked_by: []
---

# Click subdivisions and accents

## Problem

The click only marks beats, with beat 1 accented. Players practising slow
material or odd meters want eighth/sixteenth subdivisions and configurable
accents.

## Current behaviour

- `MetronomePlayer::setContext` takes sample rate, BPM, beats-per-bar (numerator)
  and `beatUnit` (denominator); it schedules a soft tick per beat and an accent
  on beat 1 (`ClickSynth`).
- There is no subdivision or per-beat accent pattern.

## Open questions (resolve before implementation)

1. Subdivision options: off / eighth / sixteenth / triplet? Fixed per meter or
   free choice?
2. Accents: just "accent beat 1" (today), a fixed alternating pattern, or a
   user-editable per-beat pattern (step UI)?
3. Distinct sounds: reuse the tick/accent voices at different gains, or add a
   third "sub" voice to `ClickSynth`?
4. Interaction with count-in, `clickDuringCapture`, and the free-running MIDI
   clock.

## Required change (proposed, pending answers)

- Extend `MetronomePlayer` scheduling to emit sub-clicks at the chosen division
  with a lower-gain voice.
- Persist the subdivision (and accent pattern if any) in the properties file
  like the other metronome settings.
- UI in the **Click sound** popover (`SyncControls.jsx`) or a small sync-strip
  cluster.

## Acceptance criteria

TBD once the open questions are answered.

## Docs

`AGENTS.md` (metronome / MIDI clock), `juce-audio` skill.

## Files

`Source/MetronomePlayer.h/.cpp`, `Source/ClickSynth.h/.cpp`,
`Source/PluginProcessor.*`, `Source/WebViewEditor.*`,
`WebUI/src/components/SyncControls.jsx`, `WebUI/src/bridge.js`,
`Tests/test_MetronomePlayer.cpp`.

## Out of scope

Different time signatures (done, 0032); a full drum machine.
