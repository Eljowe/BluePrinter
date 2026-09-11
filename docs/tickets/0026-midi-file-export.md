---
id: "0026"
title: "MIDI file (.mid) export"
status: needs-triage
blocked_by: []
---

# MIDI file (.mid) export

## Problem

`README.md` explicitly states that the looper is audio-only, that MIDI event
recording/quantization was removed, and that `.mid` export is out of scope. If
users want MIDI export, it is a large feature: the plugin currently captures only
audio (plus clock output) and discards note events.

## Current behaviour

- MIDI is passed through / filtered to chains and to the output device (clock +
  Start/Stop). No MIDI events are stored.
- The looper captures the audio record mix only.
- There is no `juce::MidiFile` writer anywhere in `Source/`.

## Required change

No implementation yet — this ticket exists to force a decision. Pick one:

1. **wontfix** — keep the documented audio-only scope.
2. **Research prototype** — a throwaway spike answering how much is needed:
   capture the input MIDI buffer, quantify against the metronome grid, and write a
   `.mid` with `juce::MidiFile`.
3. **Spec** — a full design for MIDI event capture + quantization + export,
   including what happens with per-chain MIDI filtering.

## Acceptance criteria

The decision and (if selected) the follow-up ticket are recorded here, and the
`README.md` scope statement is updated to match.

## Docs

`README.md` (Audio looper section — the "no MIDI export" statement).

## Files

TBD pending the decision.

## Out of scope

Anything until the triage decision is made.
