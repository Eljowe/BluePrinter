---
id: "0030"
title: "Undo/redo for looper overdub layers"
status: ready-for-agent
blocked_by: []
---

# Undo/redo for looper overdub layers

## Problem

Every overdub pass is mixed irrevocably into the loop buffer. A bad layer means
discarding the whole loop and re-capturing, which is destructive in the middle of a
session. Because layering is the core looper workflow, a mis-take is a frequent,
expensive mistake.

## Current behaviour

The layer-finalize path wrap-mixes a completed overdub layer into the loop region and
discards the layer audio afterwards. No previous loop contents are retained, so there
is no way back.

## Decisions (grilled)

- **History scope:** overdub layers only. Fresh captures clear history; crop changes
  are independent and are **not** part of history.
- **Model:** full pre-layer **snapshots** of the loop audio region. A mix of layers is
  not invertible, so undo always rebuilds from a known-good copy.
- **Bound:** at most **10 steps**, drop-oldest, plus a **~64 MB total-bytes ceiling**
  that drops the oldest snapshots first. The ceiling only bites on long multichannel
  loops.
- **Persistence:** session-only. Loop audio itself is not persisted, so history isn't
  either; a fresh capture clears it.
- **UI:** Undo/Redo buttons in the looper settings strip, disabled by stack depth, plus
  **Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y** scoped to the Loop tab (not app-global).
- **Disabled while playing or capturing** (including count-in and overdub); enabled
  otherwise when depth allows. Audio-buffer restore runs on the message thread under
  the existing record lock — no new audio-thread locks.
- **Redo invalidation:** a new layer or a fresh capture clears redo; a crop change does
  not.
- **Peaks / latch:** restoring recomputes the loop waveform peaks and clears the loop
  clip latch when the restored peak is below 1.0.

## Agent Brief

**Category:** enhancement
**Summary:** Add a bounded undo/redo history for looper overdub layers, so a bad layer
can be removed (and reapplied) without discarding the loop.

**Current behavior:**
A completed overdub layer is mixed permanently into the loop's audio region and the
layer source is discarded. There is no stored prior state and no user-facing way to
revert a pass.

**Desired behavior:**
Immediately before each completed overdub is mixed in, capture a snapshot of the loop
audio region. Expose undo/redo for that history:
- Undo restores the previous snapshot exactly; redo re-applies a layer that was undone.
- History holds at most 10 snapshots, and never more than the byte ceiling; the oldest
  snapshot is dropped as needed.
- A fresh loop capture clears the entire history. A crop change does not.
- History is in-memory only and does not survive a restart.
- Undo/redo are unavailable while the loop is playing, recording, counting in, or
  overdubbing, and when the corresponding stack is empty.
- Restoring a snapshot refreshes the loop's waveform peaks and updates the loop clip
  latch to match the restored content (clear it when peak < 1.0).
- Loop length, crop window, and playback-position semantics are unchanged by undo/redo.

**Key interfaces:**
- The looper state that owns the loop audio buffer gains a bounded undo stack and redo
  stack of audio snapshots, with drop-oldest eviction by step count and total bytes.
- The layer-finalize path (message thread, before the mix) takes the snapshot.
- A new pair of frontend→backend events (`frontendLoopUndo` / `frontendLoopRedo`)
  registered in the shared event-name constants and mirrored in the WebUI bridge.
- The transport snapshot gains undo/redo availability (stack depths) so the UI can
  enable/disable the controls.
- Reuse the existing loop-peak and clip-latch reset paths on restore.

**Acceptance criteria:**
- [ ] After an overdub, Undo restores the exact pre-layer loop content; Redo re-applies
      the layer.
- [ ] Redo is refused/cleared after a new layer or a fresh capture; cropping does not
      clear it.
- [ ] History never exceeds 10 steps or the byte ceiling, and evicts oldest-first with
      no unbounded growth.
- [ ] Undo/Redo are disabled while playing, recording, in count-in, overdubbing, or
      when the relevant stack is empty.
- [ ] Undo/redo leaves loop length and playback position semantics unchanged.
- [ ] Restoring refreshes loop waveform peaks and clears the loop clip latch when the
      restored peak is below 1.0.
- [ ] No allocations or locks are added to the audio callback.
- [ ] A fresh capture clears history; after restart there is no history.

## Docs

`AGENTS.md` (looper/overdub), `README.md` (looper), `juce-audio` and `webui-bridge`
skill notes.

## Files

`Source/PluginProcessor.h/.cpp` (looper state, layer finalize, snapshot/restore),
`Source/LoopPlayback.h`, `Source/WebViewEditor.h/.cpp`, `WebUI/src/components/Looper.jsx`,
`WebUI/src/bridge.js`.

## Out of scope

Per-layer volume/mute/solo; waveform-level layer editing; unbounded history; undo of
crop changes or fresh captures; persisting history across restart.
