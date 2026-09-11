---
id: "0021"
title: "MIDI footswitch / CC control with learn"
status: wontfix
blocked_by: []
---

# MIDI footswitch / CC control with learn

## Resolution

**Wontfix (2026-09-12).** The maintainer does not use a MIDI footswitch, so this
control surface is out of scope. The existing global keyboard shortcuts (0020)
cover hands-free transport. Revisit only if real users ask for pedal control;
the open questions below remain as the starting point if that happens.

## Problem

A guitarist wants to drive record/loop actions with a MIDI footswitch. The user
has not yet decided the control model (fixed bindings vs. user-learned), so this
ticket is not ready for an agent.

## Current behaviour

MIDI is used for chain input (notes, filtered per chain) and for output (clock,
Start/Stop). The standalone merges all MIDI input devices into one stream,
`AudioProcessorPlayer` discards the `MidiInput*`, JUCE 8 has no per-message source
id, and `MidiMessage` carries no device field — so per-device routing is
impossible and the MIDI channel is the only discriminator. There is no MIDI
control-message handling for transport actions.

## Open questions (resolve before implementation)

1. Fixed default CC bindings, a user-learn flow, or both?
2. Which actions are exposed: take record toggle, loop record, loop play/stop,
   clear loop, tap tempo, count-in toggle?
3. Momentary footswitches (press/release) vs. latching — how to interpret CC value
   (threshold? value 127 = trigger?).
4. Should note-on and program-change also be mappable, or CC only?
5. Persistence shape and where the learn UI lives (sync strip? a new Control
   panel?).

## Required change (proposed, pending answers)

- Add a "Control" mapping section with a learn flow: choose an action, move the
  pedal, capture the CC number (and channel).
- Persist the CC→action map in the properties file like other standalone state.
- Handle control messages in the MIDI input path and debounce momentary pedals.

## Acceptance criteria

TBD once the open questions are answered.

## Docs

`AGENTS.md` (MIDI clock / standalone MIDI notes), `juce-audio` skill.

## Files

`Source/PluginProcessor.h/.cpp`, `Source/WebViewEditor.h/.cpp`,
`WebUI/src/components/SyncControls.jsx` (or a new component),
`WebUI/src/bridge.js`.

## Out of scope

Per-device routing (not possible in the current standalone — see above);
non-MIDI hardware integration.
