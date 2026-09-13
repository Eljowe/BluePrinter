---
id: "0048"
title: "Monitor/output clip guard"
status: needs-triage
blocked_by: []
---

# Monitor/output clip guard

## Problem

The clip LEDs only *report* clipping after the fact. The master Output can still
clip the monitor, and the print can clip if a chain is hot, with no guard.

## Current behaviour

- `Meter` objects latch a clip at peak 1.0 (`Source/Meter.h`);
  `frontendResetClip` clears them.
- Output is a plain gain (`PlaybackVolume`); there is no limiter or soft clip
  anywhere.

## Open questions (resolve before implementation)

1. Guard the **monitor only** (never change the print) or the **print too**
   (destructive, changes saved takes)? The "monitor controls never change the
   print" invariant argues for monitor-only by default.
2. Brickwall limiter, soft clip/saturation, or a hard ceiling? A limiter adds
   latency (interacts with 0046).
3. Default state (off vs on), persisted or session-only?
4. Where the control lives (Monitor deck? Header?).
5. Interaction with the clip latch: does the guard clear/latch the clip
   indicator?

## Required change (proposed, pending answers)

- A small preallocated output-stage module (limiter/clipper) applied at the
  chosen point.
- Event `frontendSetClipGuard { enabled }` (+ threshold if any), snapshot field,
  UI toggle.

## Acceptance criteria

TBD once the open questions are answered.

## Docs

`CONTEXT.md` (monitor vs print), `AGENTS.md` (metering), `juce-audio` skill.

## Files

`Source/PluginProcessor.h/.cpp`, `Source/WebViewEditor.h/.cpp`,
`WebUI/src/components/…`, `WebUI/src/bridge.js`, `Tests/` (guard DSP).

## Out of scope

A full mastering limiter; auto-gain.
