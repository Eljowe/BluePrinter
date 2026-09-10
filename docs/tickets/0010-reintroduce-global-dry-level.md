---
id: "0010"
title: "Reintroduce a global Dry level (monitor + capture)"
status: done
blocked_by: []
---

# Reintroduce a global Dry level (monitor + capture)

## Problem

Ticket 0001 removed the `Dry` control and made the direct dry pass-through unity
everywhere. On a guitar chain that means the raw DI is always summed on top of the
amp/FX output — you cannot audition or print only the chain. The user wants the dry
gone from both what they hear and what they record.

## Current behaviour

- The dry pass-through is unconditionally copied into the monitor `buffer` and the
  capture `recordingMixBuffer` (`PluginProcessor.cpp` step 1b / step 2); there is no
  control over it.
- A chain's `Solo` mutes the dry in the **monitor** only (ticket 0003); the capture
  still contains dry.

## Required change

1. Re-add `dryLevel` as a standalone atomic (dB, −60..0, default 0 = unity) with
   `getDryLevel()` / `setDryLevel()` on the processor, persisted in
   `getStateInformation` / `setStateInformation` (default 0).
2. In `processBlock`, scale the monitor dry (`buffer.applyGain`) after the pristine
   `chainInputBuffer` snapshot, and scale `recordingMixBuffer` after it is copied from
   `chainInputBuffer`. Chains keep reading the full, unscaled input.
3. New `frontendSetDryLevel` event + a **Dry** knob in `HeaderControls.jsx` (dB
   −60..0), fed by `dryLevel` in the transport snapshot.
4. Leave the Input meter (raw input), REC meter (reflects the scaled print), Output
   master gain, and Loop level untouched.

## Acceptance criteria

- Dry at 0 dB is unchanged from the pre-change build (dry pass-through at unity).
- Dry at −60 dB leaves only the chains audible **and** printed in a take/loop.
- Dry never changes the chains' input or the Input meter.
- The value persists across restart and is restored with old states (absent → 0 dB).
- No dead references; `setDryLevel` documented in the skills.

## Docs

`AGENTS.md` (monitor params + routing/recording), `juce-audio` and `webui-bridge`
skill notes, `docs/tickets/0001-...` (annotate the superseded "Remove Dry" step).

## Files

`Source/PluginProcessor.h/.cpp`, `Source/WebViewEditor.h/.cpp`,
`WebUI/src/bridge.js`, `WebUI/src/App.jsx`,
`WebUI/src/components/HeaderControls.jsx`.

## Out of scope

Per-chain wet/dry blends; a dry control that is monitor-only (0001's original
concern is deliberately traded away — this knob scales the print too).

## Supersedes

The "Remove Dry entirely" step (4) of ticket 0001.
