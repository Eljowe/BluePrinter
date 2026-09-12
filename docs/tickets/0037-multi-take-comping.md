---
id: "0037"
title: "Multi-take recording and comping"
status: needs-triage
blocked_by: []
---

# Multi-take recording and comping

## Problem

The take recorder keeps exactly one pending take; a new capture invalidates it. Real
takes need several passes to choose from (or to comp the best bars), so today the
performer must save or throw away each pass before trying again.

## Current behaviour

`takePending` / `takeLength` / `takePeaks` are a single slot, auditioned via
`renderTakePlayback`, with take overdub (0023) layering onto that one take. Any new
capture invalidates the pending take.

## Required change

1. Keep a small, bounded stack of takes from consecutive passes instead of one, with
   the existing audition UI showing them all.
2. Let the user select which take to save/export, delete individual takes, and
   (stretch) comp a region from one take into another.
3. Bound memory and make it explicit when older takes are dropped.
4. Reuse `renderTakePlayback` to audition each take.
5. Decide how take overdub composes with multiple takes (recommended: overdub targets
   the selected take) and document it.

## Acceptance criteria

- Recording three passes keeps all three, each auditionable and individually savable.
- Saving or deleting one take does not disturb the others.
- Discarded takes do not reappear after restart (session-only unless saved).
- Fixed-length take capture and overdub still work.

## Docs

`AGENTS.md` (takes/overdub), `README.md`.

## Files

`Source/PluginProcessor.h/.cpp`, `Source/CaptureWrite.h`, `Source/CaptureCopy.h`,
`Source/WebViewEditor.h/.cpp`, `WebUI/src/components/TakeReview.jsx`,
`WebUI/src/components/Transport.jsx`, `WebUI/src/bridge.js`.

## Out of scope

Crossfade comping UI; per-take gain automation.
