---
id: "0012"
title: "CTest harness + pure-logic unit tests"
status: ready-for-agent
blocked_by: ["0011"]
---

# CTest harness + pure-logic unit tests

## Problem

The repo has no automated tests. Load-bearing pure logic — key detection, loop
grid quantization, snippet gain/normalize math, sidecar JSON, chain-state
migration, MIDI channel filtering — is embedded in large files and verified only
by hand. Any feature change or refactor (0027) can silently regress it.

## Current behaviour

`README.md` states there are no CTest tests; verification is manual via the
standalone app.

## Required change

1. Enable `enable_testing()` / `BUILD_TESTING` and add a test target (e.g.
   `juce_add_console_app`) plus `add_test` entries, wired into CI (0011).
2. Extract small, behaviour-preserving helper functions where a unit is currently
   trapped inside `PluginProcessor.cpp` (full decomposition is 0027 — keep these
   extractions minimal).
3. Cover at least:
   - `KeyDetector` — Krumhansl-Schmuckler correlation against synthetic tones;
   - `trimLooperToMusicalGrid` — short capture (zero-padded), exact-length, and
     overlong (truncated) cases, and the resulting loop length;
   - snippet gain/normalize math — `gainDb` clamp to −24..+24, peak → −1 dBFS;
   - snippet sidecar JSON write/read round-trip;
   - chain-state migration — legacy `midiChain`/`audioChain` split and pre-split
     single `slots` → chains, plus `ensureUniqueChainIds`;
   - MIDI channel filter bitmask (channels 1–16, system messages pass).

## Acceptance criteria

- `ctest` builds and passes locally.
- Each listed area has at least one test.
- No production behaviour changes.
- CI runs the suite (add the step to `.github/workflows/build.yml`).

## Docs

`README.md` (Testing), `AGENTS.md` (Commands — replace "no tests configured").

## Files

`CMakeLists.txt`, new `Tests/` directory, minor extraction in
`Source/PluginProcessor.cpp`, `Source/KeyDetector.cpp`.

## Out of scope

UI/WebView tests, audio-thread `processBlock` integration tests, full module
decomposition.
