---
id: "0005"
title: "Overdub clip guard: loop clip latch + overdub level"
status: done
blocked_by: ["0001"]
---

# Overdub clip guard: loop clip latch + overdub level

## Problem

Each overdub layer is summed into the loop with no headroom management, so repeated
layers can push the loop past 0 dBFS silently, and the loop's clip state is invisible.

## Required change

- Latch a `loopClipped` flag when loop playback or the `mixOverdubLayer` result reaches
  >= 0 dBFS; surface it on the loop waveform/meter with click-to-reset (shares the
  clip-latch work from 0002).
- Add an `overdubLevel` trim (dB, `-60..0`, default 0) applied when `mixOverdubLayer`
  sums the new layer, so layers can be attenuated before mixing. Persisted like
  `loopLevel`; new `frontendSetOverdubLevel` event + a "Dub" knob in `Looper.jsx`.

## Acceptance criteria

- A clipping loop lights a latched indicator.
- Overdub level scales each new layer before it is mixed; 0 dB is a no-op.
- The setting persists across restart.

## Docs

Update `AGENTS.md` (looper/overdub) + the `webui-bridge` skill.

## Files

`Source/PluginProcessor.h/.cpp`, `Source/WebViewEditor.h/.cpp`,
`WebUI/src/bridge.js`, `WebUI/src/components/Looper.jsx`, `WebUI/src/App.jsx`,
`WebUI/src/styles.css`.

## Out of scope

Automatic gain/limiting of the loop; per-layer undo.
