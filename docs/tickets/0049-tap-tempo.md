---
id: "0049"
title: "Tap tempo"
status: needs-triage
blocked_by: []
---

# Tap tempo

## Problem

BPM is only settable by dragging/typing the knob, so a guitarist cannot match
the tempo of an external riff or a backing track by ear.

## Current behaviour

- The header **BPM** knob emits `frontendSetBpm`; the value feeds the take
  recorder, the looper's beat math and the MIDI clock.
- No tap control or tempo averaging exists.

## Open questions (resolve before implementation)

1. Placement: a Tap button beside the BPM knob (header) or in the sync strip?
2. Averaging: last N taps (N=?), median/mean, reset after a timeout (how long)?
3. Keyboard shortcut (e.g. `T`), and does it work while a take/loop is
   capturing?
4. Does tapping during clock playback re-align the MIDI clock phase / restart
   the click?
5. Range clamp and rounding (whole BPM? halves?) for display.

## Required change (proposed, pending answers)

- Track tap timestamps on the message thread, compute BPM, call the existing
  `frontendSetBpm` path.
- A **Tap** button (and optional shortcut); no new backend state beyond the
  existing BPM persistence.

## Acceptance criteria

TBD once the open questions are answered.

## Docs

`AGENTS.md` (BPM / MIDI clock), `webui-bridge` skill.

## Files

`WebUI/src/components/HeaderControls.jsx` (or `SyncControls.jsx`),
`WebUI/src/App.jsx`, `WebUI/src/bridge.js`; possibly
`Source/PluginProcessor.*` if clock re-alignment is added.

## Out of scope

Tempo maps/automation; audio tempo detection (see 0051).
