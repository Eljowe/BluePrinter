---
id: "0047"
title: "Per-chain CPU and reported latency readout"
status: needs-triage
blocked_by: []
---

# Per-chain CPU and reported latency readout

## Problem

There is no visibility into what each chain costs or how much latency it adds.
When a take smears or the CPU spikes, the user cannot tell which chain is
responsible.

## Current behaviour

- `backendVst3Chain` ships per-chain `volume`, `muted`, `pending`, `slots`;
  `backendTransport` ships `chainLevels` (level/peak) at 30 Hz.
- Nothing measures chain process time or reads slot `getLatencySamples()`.

## Open questions (resolve before implementation)

1. CPU measurement: wall-clock around each chain's process in `processBlock`
   (smoothed against the block budget), or rely on
   `AudioProcessor::getCpuUsage()`? Wall-clock is direct but adds
   `steady_clock` calls per block.
2. Is CPU meaningful in a DAW, where the host schedules the plugin's process?
   Show latency always and CPU only in the standalone, or show both with a
   caveat?
3. UI placement: on the chain card, in a tooltip, or a combined "performance"
   popover? Pair with 0046's latency display.
4. Push cadence/shape: extend `chainLevels` entries or add a `chainStats` array.

## Required change (proposed, pending answers)

- `PluginChain` accumulates smoothed process-time and sums active-slot latency;
  `PluginProcessor` pushes both in the 30 Hz snapshot.
- `PluginChain.jsx` renders a small CPU % + latency (ms/samples) readout.

## Acceptance criteria

TBD once the open questions are answered.

## Docs

`AGENTS.md` (chain snapshot / `chainLevels`), `webui-bridge` skill.

## Files

`Source/PluginChain.h/.cpp`, `Source/PluginProcessor.cpp`,
`Source/WebViewEditor.cpp`, `WebUI/src/components/PluginChain.jsx`,
`WebUI/src/bridge.js`.

## Out of scope

A full performance profiler; per-slot (not per-chain) attribution.
