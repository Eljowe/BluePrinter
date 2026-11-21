# BluePrinter Development Instructions

This file is the always-loaded rulebook: project overview, build/test commands, the source map, the
hard conventions, the chain ownership/restore invariants and a pointer per area. Implementation
mechanics and incident history live in [`docs/architecture/`](docs/architecture/README.md) and load
on demand. `CONTEXT.md` holds the language, module map and invariants; `docs/adr/` holds the
load-bearing decisions.

## Project Overview

BluePrinter is a JUCE audio plugin (VST3 + Standalone; AU on macOS) for recording guitar takes with
VST3 FX chain hosting, a React WebView UI (WebView2 on Windows, WKWebView on macOS, WebKitGTK on
Linux), and snippet library management. Snippets are saved as WAV files with JSON sidecars.
**Cross-platform** (ADR-0006): Windows, macOS (universal, 11+) and Linux (Ubuntu 22.04+, WebKitGTK
4.1). The Windows-only WebView2 patch/installer/crash tooling are per-platform now.

## Where to read next

- [`docs/architecture/audio-path.md`](docs/architecture/audio-path.md) — chain model, routing/MIDI
  limits, PDC, recording and capture (loops, takes, overdub, undo), stems, meters/clip guard, tuner
  and melody, MIDI clock/click/rehearsal, bus layout.
- [`docs/architecture/chain-restore.md`](docs/architecture/chain-restore.md) — chain
  serialisation/migration, parameter persistence, the deferred restore driver, crash marker and
  self-heal, crash diagnostics, plugin quarantine, presets, plugin manager, editor windows.
- [`docs/architecture/webui.md`](docs/architecture/webui.md) — recording tabs, shortcuts,
  header/sync controls, bridge events and payloads, library/setlists, Scales/Chords, accessibility,
  resize/maximise, UI scaling, splash, song builder and rehearsal.
- [`docs/architecture/build-and-ci.md`](docs/architecture/build-and-ci.md) — JUCE/WebView2 patch,
  ASIO, output formats, VS Code tasks, release scripts, CI jobs, lint/format.

## Build System

- JUCE source tree expected at `C:/JUCE/JUCE` (override with `-DJUCE_DIR=...`), **JUCE 8.0.14 on
  Windows plus the patch in `cmake/patches/juce-webview2-additional-args.patch`**
  (`WinWebView2::withAdditionalBrowserArguments`, used by `WebViewEditor.cpp` to pass
  `--allow-no-sandbox-job --disable-gpu`). Apply with `git -C <juce> apply cmake/patches/juce-webview2-additional-args.patch`;
  CI does this after checkout **on Windows only** — macOS/Linux use stock JUCE (the patch touches
  WinWebView2 and the call site is `#if JUCE_WINDOWS`).
- CMake configure/install rules: `CMakeLists.txt` (see
  [`build-and-ci.md`](docs/architecture/build-and-ci.md)).
- WebUI must be built separately: `cd WebUI && npm install && npm run build`

Output formats (VST3 + Standalone everywhere; AU on macOS), ASIO, the VS Code tasks, CI jobs,
packaging and release scripts are in
[`docs/architecture/build-and-ci.md`](docs/architecture/build-and-ci.md).

## Source Code

- All C++ source in `Source/` directory
- React source in `WebUI/src/` (entry `main.jsx`, root `App.jsx`, all components in
  `WebUI/src/components/`)
- Entry point: `PluginProcessor.h/.cpp` (audio), `WebViewEditor.h/.cpp` (UI bridge), `App.jsx`
  (frontend)

## Coding Conventions

- C++: Use `#pragma once`, `#include <JuceHeader.h>`, JUCE coding style. No raw pointers for owned
  objects — use `std::unique_ptr`, or `std::shared_ptr` where a second thread may still reference
  the object (chain/slot snapshots, snippet audio).
- Threading: Audio thread vs message thread. Use `std::atomic` for cross-thread flags. **No blocking
  lock and no allocation in `processBlock`**: the chain and slot snapshots are refilled under
  `tryEnter()` with a previous-snapshot fallback (see VST3 Chains), and removed objects are
  reclaimed on the message thread only after the audio thread's snapshot has dropped them.
- JavaScript: React functional components with hooks. Use `bridge.js` for all C++ communication.
  Never hardcode event name strings.
- APVTS exposes `Gain` (display **Input**, −12..+24 dB, default 0) and `PlaybackVolume` (display
  **Output**, −60..+12 dB, default 0; the ID is kept for saved-state compatibility). Input is the
  record trim applied first in `processBlock`; Output is the master monitor gain applied last, so it
  never changes the capture. The **Dry** control (`dryLevel`, standalone atomic, dB −60..0, default
  0 = unity) scales the direct dry pass-through in **both** the monitor mix and the capture
  (`recordingMixBuffer`), so at −60 dB only the chains are heard and printed; the chains always
  receive the full input.

The looper Loop level, session-only Reverse/Half-speed, and the metronome/BPM/count-in/click/MIDI
settings are standalone atomics + `PropertiesFile` state, never APVTS; see
[`audio-path.md`](docs/architecture/audio-path.md#levels-and-session-only-settings).

The Tuner/Scales/Chords header tools share one clean-input YIN pitch analysis off the audio thread;
see [`audio-path.md`](docs/architecture/audio-path.md#built-in-tuner-the-analysis-worker-0034) and
[`webui.md`](docs/architecture/webui.md#header-tools-scales-and-chords).

Take and library-snippet melodies are extracted offline with `MelodyAnalyzer` and auditioned
monitor-only; see
[`audio-path.md`](docs/architecture/audio-path.md#take-melody-extraction-and-audition-0054).

IN/REC/OUT/loop meters, clip latches and the monitor-only Guard are in
[`audio-path.md`](docs/architecture/audio-path.md#metering-clip-latches-and-the-monitor-guard-0048).

Snippet gain/favourite/seeking/export/import, setlists and the song UI live in
[`webui.md`](docs/architecture/webui.md); stems and load-into-looper in
[`audio-path.md`](docs/architecture/audio-path.md).

## Commands

- Build WebUI: `cd WebUI && npm install && npm run build` (outputs `WebUI/dist/`)
- Build C++ (Debug): CMake configure with JUCE_DIR, then `cmake --build build --config Debug` — see
  `.vscode/tasks.json` for the exact invocation.
- There **is** a pure-logic CTest suite: `cmake -S . -B build -DBUILD_TESTING=ON && cmake --build build --target BluePrinterTests && ctest --test-dir build --output-on-failure`
  (sources in `Tests/`, no external framework; CI runs it on every push/PR).
- No `.clang-format`, ESLint or Prettier config: match the surrounding style by hand. Full test/CI
  notes are in [`build-and-ci.md`](docs/architecture/build-and-ci.md).

## Event Naming

All C++→JS and JS→C++ events use `static constexpr const char*` in `WebViewEditor.h`. Match these
exactly in `bridge.js` (`FRONTEND_EVENTS` / `BACKEND_EVENTS`). Never hardcode event name strings.
Parity is machine-checked by `Tests/check-bridge-events.mjs` (CTest case `BridgeEventParity`, run in
CI), so a one-sided rename fails the build with a readable diff. See the **webui-bridge** skill for
the full event listing and the add-a-new-event recipe.

## VST3 Chains (multi-chain model)

- The plugin hosts a **flexible list of parallel chains** (not a fixed MIDI + audio pair). Chains
  live in `std::vector<std::shared_ptr<PluginChain>> chains` on the processor, guarded by
  `chainLock`. The audio thread refreshes a `shared_ptr` snapshot (`blockChains`, reserved to
  `maxChains = 64`) with `chainLock.tryEnter()` at the top of `processBlock`; if the lock is busy it
  reuses the previous block's snapshot (its shared_ptrs keep those chains alive). The audio thread
  therefore never blocks and never allocates there. **Removal is deferred against snapshot refills,
  never wall-clock time**: `removeChain`/`clearChains` move the chain into `retiredChains` with
  `deadlineGeneration = chainSnapshotGeneration + 2` (that counter is bumped only by a *successful*
  `blockChains` refill), and `timerCallback` → `collectRetiredChains()` drops the `shared_ptr` once
  two refills have happened — so the audio thread has already dropped the chain from its snapshot
  and the plugin is destroyed on the message thread. `PluginChain` does the same per slot
  (`slots`/`blockSlots` are `shared_ptr`, `removePlugin`/`clear` retire into `retiredSlots`,
  `collectRetiredSlots()` uses the same two-refill rule against `slotSnapshotGeneration`). If audio
  is paused (or a chain is skipped because it has no active plugins) the object is simply retained —
  bounded, and it prevents a release while a snapshot still holds it.
  `prepareToPlay`/`releaseResources` clear `blockChains`/`blockSlots` with audio stopped and force a
  collection, so a stopped device never retains them. Chains/slots with an async plugin load in
  flight (`hasLoadsInFlight()`) are not collected, because the loader holds a raw pointer to the
  chain; `ChainStatePersistence::createChain` refuses past `maxChains` (returning `nullptr`) so a
  restored state can't exceed the audio snapshot's capacity.
- **Solo** (`frontendSetChainMonitorSolo`): if any chain is soloed, the monitor mix becomes only the
  soloed chains and the direct dry pass-through is cleared (true isolation); **monitor-mute**
  (`frontendSetChainMonitorMute`) drops just that chain from the monitor. Neither touches
  `recordingMixBuffer` (capture still follows the hard Mute + Record flags); the chain output meter
  stays post-volume and independent of solo/mute. Both persist with the chain bundle.
- **Routing** (PluginProcessor.cpp `processBlock`): gain → `chainInputBuffer` (pristine dry
  snapshot) → chains run in parallel on a shared scratch buffer (`chainScratchBuffer`), each copying
  its selected channels **from `chainInputBuffer` — never from the accumulating mix**, so chains can
  never process each other's output (the selected channels are **compacted** onto the chain's first
  channels by `ChainRouting::copyInputChannels`, so any input — e.g. a mic on input 4 — can feed a
  mono/stereo plugin, which only reads its first channels). Each chain's output is summed into the
  main mix (× its volume, unless muted) alongside the dry post-gain input (× the Dry level).
  **Chains with no active (non-bypassed) plugins are skipped entirely**
  (`PluginChain::hasActivePlugins`) — a transparent chain's scratch is just a dry copy, and summing
  it back would double the dry signal. MIDI: each chain gets its own copy of the input MIDI buffer
  (`chainMidiScratch`), filtered by its channel mask into `chainMidiFiltered` when needed — no
  cross-chain MIDI flow; the host's output MIDI stays as raw input (+ clock events).
- **Plugin delay compensation** (0046): the dry pass-through and every parallel chain are aligned to
  one uniform latency `L` before summing, in **both** the monitor and the print.
- **Recording**: captures (`recordingMixBuffer`) = the PDC-aligned dry pass-through (× the Dry
  level) + the PDC-aligned outputs of chains with `recordOnCapture` on (muted chains excluded). Both
  the take recorder and the looper tap this mix, so a synth chain can be excluded from a guitar
  take.

Per-chain fields/defaults, the CPU/latency readout and the standalone MIDI-merge limitation are in
[`audio-path.md`](docs/architecture/audio-path.md#chain-model); PDC and capture mechanics are in
[`audio-path.md`](docs/architecture/audio-path.md#plugin-delay-compensation-0046).

**Restore and persist rules.**

- `applyChainState` suppresses `persistPluginChain` for its whole duration so a restore can never
  echo a partial state back into the properties file (the JUCE standalone calls
  `setStateInformation` at startup via `reloadPluginState`, and an unguarded echo used to wipe the
  saved chains). **Persist + flush are also gated by `isChainRestoreInProgress()`** (`restoreActive`
  / `pendingPluginLoads` / a queued state-blob apply / any chain with pending slots): while the
  deferred restore is still loading, the in-memory chains are partial or empty, and a save would
  replace the good on-disk state with the partial one (this wiped a user's 5-slot state once).
  `persistPluginChain` skips arming, `flushPendingChainPersist` drops stale arms — the on-disk state
  is already correct, and the next user mutation after the restore completes persists normally.
  **`getStateInformation` omits the `pluginChains` property from the state blob entirely while
  `isChainRestoreInProgress()`, and for the whole launch while `stateRestoreSkippedThisLaunch`**
  (self-heal: state blobs skipped and/or a plugin quarantined, so the in-memory plugins hold
  DEFAULTS) — the JUCE standalone writes this blob to its OWN settings file
  (`%APPDATA%\BluePrinter\BluePrinter.settings`, separate from `BluePrinter.properties`) on every
  exit, so quitting mid-restore used to capture the partial chains there and the next launch
  restored *that* empty state (and propagated it into the properties file); quitting a self-heal
  launch used to capture the DEFAULT plugin states and the next launch restored *those* (the "state
  lost on close" wipe — closing with one Cory Wong X loaded, its defaults came back next launch).
  Omitting it makes the next launch fall back to the properties file, which still holds the last
  good state. Deleting a poisoned `BluePrinter.settings` is safe (it only holds filterState + window
  prefs).

Chain serialisation/migration, the deferred restore driver, crash marker/self-heal, quarantine,
crash diagnostics, named presets, the plugin manager and editor-window ownership are in
[`chain-restore.md`](docs/architecture/chain-restore.md).

## Skills

This project has four OpenCode skills configured:

- **blueprinter**: General project knowledge, architecture, conventions
- **webui-bridge**: React WebUI and C++ WebView2 bridge development
- **juce-audio**: JUCE audio plugin patterns specific to this project
- **hallmark**: Third-party anti-AI-slop design skill (from github.com/Nutlope/hallmark) for
  greenfield UI pages, audits, redesigns, and design extraction. Not BluePrinter-specific — triggers
  on UI/landing-page design requests.

## Agent skills

### Issue tracker

Issues and specs live as local markdown files under `docs/tickets/` (one file per ticket, indexed in
`docs/tickets/README.md`); versioned with the code, no `gh`. See `docs/agents/issue-tracker.md`.

### Triage labels

Five canonical roles, default labels: `needs-triage`, `needs-info`, `ready-for-agent`,
`ready-for-human`, `wontfix`. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: [`CONTEXT.md`](CONTEXT.md) (language, module map, invariants) and
[`docs/adr/`](docs/adr/) (load-bearing decisions — cross-platform/WebView2, deferred one-per-turn
restore, chains-never-hear-each-other, persist gating, audio-only looper). Read those before
exploring; deep implementation detail lives in [`docs/architecture/`](docs/architecture/README.md).
See `docs/agents/domain.md`.
