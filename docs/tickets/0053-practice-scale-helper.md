---
id: "0053"
title: "Practice helper: scale/chord overlay from the detected key"
status: needs-triage
blocked_by: []
---

# Practice helper: scale/chord overlay from the detected key

## Problem

`KeyDetector` already estimates a snippet's key, and the tuner tracks the live
note, but neither is turned into a practice aid (which scale tones fit, where to
find them).

## Current behaviour

- `KeyDetector::detectKey` returns key, confidence and detected pitch classes
  for a snippet; the tuner publishes the live frequency/note.
- The library shows detected keys; there is no scale/chord or fretboard view.

## Open questions (resolve before implementation)

1. Input: the selected snippet's detected key, the live input (tuner ring), or a
   manually chosen key?
2. Presentation: guitar fretboard diagram, note-name strip, or scale-degree
   readout? Which tuning(s)?
3. Chords too (diatonic triads/sevenths) or scales only for v1?
4. Where it lives: extend the Tuner popover, its own header popover, or the
   snippet card?
5. Does it need `KeyDetector` to accept live audio rather than a stored buffer?

## Required change (proposed, pending answers)

- A pure scale/chord theory module (unit-tested) mapping key → scale/chord tones.
- A presentational component (fretboard/notes) fed by the detected key and/or the
  live note.

## Acceptance criteria

TBD once the open questions are answered.

## Docs

`CONTEXT.md` (new term), `webui-bridge` skill.

## Files

WebUI-first (`WebUI/src/components/…`, `WebUI/src/utils.js`); possibly
`Source/KeyDetector.*` for live input and a bridge event for the session key.

## Out of scope

Full tab/notation; audio-to-MIDI transcription.
