---
id: "0003"
title: "Monitor-only chain solo/mute"
status: done
blocked_by: []
---

# Monitor-only chain solo/mute

## Problem

A chain's `Mute` excludes it from both the monitor and the capture
(`recordingMixBuffer`). There is no way to audition a chain (or silence one) while
still recording it, forcing a choice between monitoring and print.

## Required change

- Add `monitorSolo`/`monitorMuted` atomics to `PluginChain`, serialized with the other
  chain flags (`PluginChain.cpp:510-512`, `579-586`), included in the WebView chain
  snapshot and `App.jsx` `normalizeChain`.
- In `processBlock` (`:733-804`): if any chain is soloed, only soloed chains
  contribute to the monitor mix and the direct dry pass-through is muted (true
  isolation). `monitorMuted` excludes just that chain from the monitor. Neither
  affects `recordingMixBuffer`, which continues to follow the hard `Mute` + `Record`
  flags.
- Chain output meters stay post-volume, independent of solo/mute.
- UI: two small icon toggles (solo, monitor-mute) in the chain controls row, distinct
  from the existing hard Mute.
- Events `frontendSetChainMonitorSolo` `{ chain, solo }` and
  `frontendSetChainMonitorMute` `{ chain, muted }`.

## Acceptance criteria

- Solo isolates a chain in the monitor and does not change a take/loop capture.
- Monitor-mute silences only the monitor contribution.
- Hard Mute still excludes from the capture.
- Flags persist across restart.

## Docs

Update `AGENTS.md` (chain model) + the `webui-bridge` skill.

## Files

`Source/PluginChain.h/.cpp`, `Source/PluginProcessor.h/.cpp`,
`Source/WebViewEditor.h/.cpp`, `WebUI/src/bridge.js`, `WebUI/src/App.jsx`,
`WebUI/src/components/PluginChain.jsx`, `WebUI/src/styles.css`.

## Out of scope

Solo across takes/snippets; changing the hard Mute semantics.
