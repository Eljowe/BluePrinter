---
id: "0027"
title: "Decompose PluginProcessor into deep modules"
status: needs-triage
blocked_by: ["0011", "0012"]
---

# Decompose PluginProcessor into deep modules

## Problem

`Source/PluginProcessor.cpp` is 4009 lines / 166 KB and `PluginProcessor.h` is 887
lines. It mixes APVTS parameters, per-block routing, the take recorder, the looper,
the metronome, the MIDI clock, metering, chain persistence, deferred restore,
plugin quarantine/self-heal, and crash diagnostics. It is the highest-risk and
hardest-to-navigate file in the repo, and every feature ticket above must edit it.

## Current behaviour

One class owns all of the above, with the audio thread calling into methods defined
in the same translation unit that also contains the large persistence/restore code.

## Required change

Use the `codebase-design` skill to pick seams, then extract cohesive deep modules
one at a time, each behind tests from 0012. Candidate modules:

- `ChainStatePersistence` (make/apply chain state, migration, quarantine/self-heal)
- `TakeRecorder`
- `Looper` (capture, grid trim, crop, overdub mix)
- `MetronomeClock`
- `MidiClock`
- `RecordingBuffer`
- `Metering`

Rules: incremental (one module per PR), behaviour-preserving, no new locks or
allocations on the audio thread, and `processBlock` reduces to an orchestrator.

## Acceptance criteria

- Each extracted module has a documented interface (in `CONTEXT.md`/ADRs from
  0014) and unit tests where the logic is pure.
- `processBlock` reads as a sequence of named steps rather than inline detail.
- No audio-thread allocation or locks introduced; existing behaviour preserved.
- CI (0011) + tests (0012) stay green throughout; manual smoke test per extraction.

## Docs

`AGENTS.md` (module map), `CONTEXT.md` (0014), `codebase-design` skill outputs.

## Files

`Source/PluginProcessor.h/.cpp`, plus new module files under `Source/`, and
`CMakeLists.txt` if sources are listed explicitly.

## Out of scope

Changing behaviour, public event names, or the on-disk state format. This is a
structural refactor only.
