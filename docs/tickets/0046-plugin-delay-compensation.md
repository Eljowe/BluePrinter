---
id: "0046"
title: "Plugin delay compensation for parallel chains"
status: needs-triage
blocked_by: []
---

# Plugin delay compensation for parallel chains

## Problem

The plugin mixes multiple parallel chains plus the dry pass-through with no
latency alignment. Any hosted plugin that reports latency (amp sims with
oversampling, lookahead limiters, linear-phase EQ) makes its chain arrive late,
so the monitor/print sum exhibits comb filtering and the chains drift relative to
each other. There is no latency handling anywhere in the codebase.

## Current behaviour

- `processBlock` iterates `blockChains`; each chain processes a copy of
  `chainInputBuffer`, and every chain's output is summed with the dry post-gain
  input. No delay is inserted anywhere.
- `PluginChain` never reads `AudioProcessor::getLatencySamples()` from its slots.
- The processor never calls `setLatencySamples`, so a DAW is unaware of any
  internal latency.

## Open questions (resolve before implementation)

1. Compensate the monitor too, or only the capture bus? The monitor is what the
   player hears while tracking, so monitor alignment is likely the bigger win.
2. Report the compensation to the host via `setLatencySamples` (shifts the DAW
   timeline / project PDC) or keep it internal? Internal-only leaves the capture
   late relative to the DAW grid.
3. Latency source: sum `getLatencySamples()` over active (non-bypassed) slots
   only? Plugins may still delay when bypassed.
4. Plugins that change latency at runtime (e.g. toggling oversampling): recompute
   per slot mutation, or every block?
5. Memory cap for the alignment delay lines (a plugin can report a huge latency).
6. Should the click, loop playback and take-overdub monitor also be shifted so
   they align with the compensated input?

## Required change (proposed, pending answers)

- New pure module `Source/DelayCompensation.h` (unit-tested): given per-source
  latencies, compute each source's compensating delay and the maximum.
- `PluginChain` exposes its active latency (sum of slot latencies).
- `processBlock` delays each chain output and/or the dry path into preallocated
  delay lines; no allocation, no locks.
- `setLatencySamples` wiring if the host-report option is chosen.

## Acceptance criteria

TBD once the open questions are answered.

## Docs

`CONTEXT.md` (routing invariants), `docs/adr/` (host-reported vs internal PDC),
`AGENTS.md` (routing section), `juce-audio` skill.

## Files

`Source/PluginProcessor.h/.cpp`, `Source/PluginChain.h/.cpp`,
`Source/DelayCompensation.h` + `Tests/test_DelayCompensation.cpp`.

## Out of scope

Out-of-process plugin sandboxing; per-plugin latency editing.
