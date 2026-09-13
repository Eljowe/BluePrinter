---
id: "0027"
title: "Decompose PluginProcessor into deep modules"
status: in-progress
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

## Plan (seams)

Triage picked the order below: the pure/small seams first, the stateful
audio-path modules next, and the large coupled persistence last. Each row is
its own PR — behaviour-preserving, pure logic under test, CI green before the
next.

| # | Module | Seam | Status |
| - | ------ | ---- | ------ |
| 1 | `ClickSynth` | Pure click-voice synthesis behind `Voice` + `render` | done |
| 2 | `MetronomePlayer` | Per-block click scheduling + ring-out (owns `activeClicks`) | done |
| 3a | `MidiClockMath` | 24-ppqn pulse scheduling (pure) | done |
| 3b | `MidiClockOutput` | Device open/close + Start/Stop + direct send | done (device smoke test pending) |
| 4a | `MeterMath` | Peak/RMS measurement + level smoothing + peak decay (pure) | done |
| 4b | `Meter` | Per-meter atomics + clip latches | done |
| 5a | `RestoreSelfHeal` | Blob gating, crash suspects, quarantine | done |
| 5b | `ChainStatePersistence` | Bundle format, load key, chain construction/ids | done |
| 6a | `LooperGrid::computeCrop` | Whole-beat crop computation (pure) | done |
| 6b | `TakeRecorder` | Capture/finalize/pending-take/overdub state | done |
| 6c | `Looper` | Capture, grid trim, crop, overdub mix | done |

Step 1 removed the click-synthesis lambda from
`PluginProcessor::resynthesizeClicks` and moved it to
`Source/ClickSynth.{h,cpp}` (deterministic output, clamping in the module,
direct unit tests in `Tests/test_ClickSynth.cpp`, including a golden
sample-for-sample comparison against the original algorithm) without
touching the audio render path.

Step 6b moved the take-recorder state machine and pending-take review to
`Source/TakeRecorder.{h,cpp}`: the module owns every take-related atomic
(recording request/state, finalize-pending, the write cursor, the pending
take + review playback, and the overdub flags) and the downsampled review
peaks. The shared capture buffer (`recordBuffer`) and its lock stay in the
processor (the looper uses them too), so the processor keeps its existing
lock discipline and passes the buffer into `write`, `renderReview` and
`renderOverdubMonitor`. Library I/O (save/discard), the overdub wrap-mix and
the clock side effects stay in the processor as orchestration. Direct unit
tests in `Tests/test_TakeRecorder.cpp` cover the fresh/overdub start paths,
the fill→finalize transition, one-shot review playback and `clearPending`;
the full plugin (Debug standalone) builds and the suite stays green.

Step 6c moved the audio looper's state and operations to
`Source/Looper.{h,cpp}`: capture arming/recording (fresh and overdub), the
loop descriptor (start / length / full length / playhead / playing /
looping), fixed-length auto-stop, whole-beat crop, grid trim, reverse /
half-speed flags, the waveform peaks and the loop-layer undo/redo stacks.
The shared capture buffer and `recordLock` stay in the processor (the take
recorder uses them too), as do the loop-level gain, crossfade length, the
click/metronome clock, the loop meter and the overdub wrap-mix
(`mixOverdubLayer`, shared with the take's finalize). Buffer-touching
history ops take the buffer by reference so the processor keeps its lock
discipline. Direct unit tests in `Tests/test_Looper.cpp` cover
capture/auto-stop, overdub geometry, one-shot playback, count-in begin,
reversible crops and undo/redo; the Debug standalone builds and the suite
(110 tests) stays green.

Step 5 was split (the persistence module is large and delicate). Step 5a moved
the crash self-heal bookkeeping to `Source/RestoreSelfHeal.{h,cpp}`: the
once-per-launch decision to trust saved state blobs (marker freshness against
the last clean exit), the crash diagnostics parsing (`crash-info.txt` and the
`lastPluginLoadOp` breadcrumb that covers fail-fast crashes), the persisted
plugin quarantine list and the skipped-plugin error composition. It talks only
to the properties file and the crash-info / settings files, so it is unit
tested with a temp directory (`Tests/test_RestoreSelfHeal.cpp`). The processor
keeps the global crash handler + crash-op buffer, the chain make/apply code and
the deferred-restore driver.

Step 5b moved the chain bundle format and chain construction to
`Source/ChainStatePersistence.{h,cpp}`: `makeState`/`applyState` (library
prefilter, `ChainStateMigration::normalise` application, `nextChainId` and id
de-duplication), `loadSavedState` (the `pluginChains`-over-`pluginChain` key
preference) and `createChain`/`clearChains`/`ensureUniqueChainIds` (wiring the
chain `onChanged` persist callback through an injected `Deps`). It holds
references to the live chain list / lock / library / id counter, so the audio
thread keeps iterating the same containers; the processor keeps the restore
guard, the self-heal plan, the crash-marker lifecycle and the deferred-restore
driver. The module depends on `PluginChain` (which the pure-logic test target
excludes), so it is covered by the full plugin build; the Debug standalone
builds clean and the 116-test suite stays green.

All plan seams (1–6c, 5a/5b) are now extracted. The one remaining acceptance
item is the **manual smoke test** per extraction (audio device, hosted VST3s,
restore/recovery) — that needs a human on Windows, so the ticket stays
`in-progress` until it is done.

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
