# BluePrinter Development Instructions

## Project Overview
BluePrinter is a JUCE audio plugin (VST3 + Standalone) for recording guitar takes with VST3 FX chain hosting, a React/WebView2 UI, and snippet library management. Snippets are saved as WAV files with JSON sidecars. Windows-only (WebView2 requirement).

## Build System
- CMake 3.22+ with JUCE CMake API
- JUCE source tree expected at `C:/JUCE/JUCE` (override with `-DJUCE_DIR=...`)
- Output formats: VST3, Standalone
- See `CMakeLists.txt` for full build config and install rules
- WebUI must be built separately: `cd WebUI && npm install && npm run build`

## VS Code Tasks
See `.vscode/tasks.json` for 7 pre-configured tasks:
- Install WebUI deps, Build WebUI
- Build/Rebuild BluePrinter (Debug)
- Run BluePrinter (Debug/Release)
- Build + Install + Run (Release, per-user)

## Source Code
- All C++ source in `Source/` directory
- React source in `WebUI/src/` (entry `main.jsx`, root `App.jsx`, all components in `WebUI/src/components/`)
- Entry point: `PluginProcessor.h/.cpp` (audio), `WebViewEditor.h/.cpp` (UI bridge), `App.jsx` (frontend)

## Coding Conventions
- C++: Use `#pragma once`, `#include <JuceHeader.h>`, JUCE coding style. No raw pointers for owned objects — use `std::unique_ptr` (or `std::shared_ptr` for shared snippet audio).
- Threading: Audio thread vs message thread. Use `std::atomic` for cross-thread flags. No locks in `processBlock`.
- JavaScript: React functional components with hooks. Use `bridge.js` for all C++ communication. Never hardcode event name strings.
- APVTS currently exposes only `Gain`. Metronome/BPM/count-in/click-sound/MIDI-clock/MIDI-device settings are standalone user state (`juce::PropertiesFile`) + atomics, **not** APVTS parameters.

## Commands
- Build WebUI: `cd WebUI && npm install && npm run build` (outputs `WebUI/dist/`)
- Build C++ (Debug): CMake configure with JUCE_DIR, then `cmake --build build --config Debug` — see `.vscode/tasks.json` for the exact invocation.
- **Lint / format / test**: none configured. There is no `.clang-format`, ESLint, or Prettier config, and no test suite. Match the style of surrounding code by hand. If a lint/test command is added later, update this section.

## Event Naming
All C++→JS and JS→C++ events use `static constexpr const char*` in `WebViewEditor.h`. Match these exactly in `bridge.js` (`FRONTEND_EVENTS` / `BACKEND_EVENTS`). Never hardcode event name strings. See the **webui-bridge** skill for the full event listing and the add-a-new-event recipe.

## VST3 Chains (multi-chain model)
- The plugin hosts a **flexible list of parallel chains** (not a fixed MIDI + audio pair). Chains live in `std::vector<std::unique_ptr<PluginChain>> chains` on the processor, guarded by `chainLock`; the audio thread iterates a raw-pointer snapshot (`blockChains`) taken under that lock at the top of `processBlock`.
- Each chain has a **stable id** (`"chain0"`, `"chain1"`, … generated from `nextChainId`, persisted), a **user-editable name**, an **input mask** (any subset of the 1–8 channel input bus, `ChainInputBits`), a **MIDI toggle** (`wantsMidi`), a **MIDI channel filter** (`midiChannelsMask`, channels 1–16; system messages always pass), a **Record toggle** (`recordOnCapture`), a **volume** (dB, −60..+12, default 0), a **mute** toggle, and an **output level meter** (`outputLevel`/`outputPeak` atomics, public, written per block post-volume, pushed at 30 Hz via `chainLevels` in the transport snapshot). New chains default to: inputs both, wantsMidi on, all MIDI channels, record on, 0 dB, unmuted.
- **Routing** (PluginProcessor.cpp `processBlock`): gain → `chainInputBuffer` (pristine dry snapshot) → chains run in parallel on a shared scratch buffer (`chainScratchBuffer`), each copying its selected channels **from `chainInputBuffer` — never from the accumulating mix**, so chains can never process each other's output. Each chain's output is summed into the main mix (× its volume, unless muted) alongside the dry post-gain input. **Chains with no active (non-bypassed) plugins are skipped entirely** (`PluginChain::hasActivePlugins`) — a transparent chain's scratch is just a dry copy, and summing it back would double the dry signal. MIDI: each chain gets its own copy of the input MIDI buffer (`chainMidiScratch`), filtered by its channel mask into `chainMidiFiltered` when needed — no cross-chain MIDI flow; the host's output MIDI stays as raw input (+ clock events). **The standalone merges all MIDI input devices into one stream** (`AudioProcessorPlayer` discards the `MidiInput*`), JUCE 8 has no per-message source ID, and `MidiMessage` carries no device field — so the MIDI channel is the only discriminator the plugin can see. Per-device routing would require a custom standalone app (`JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP`); per-chain device filters don't exist by design. Channels beyond the block's count (e.g. "Right" in mono) receive silence.
- **Recording**: captures (`recordingMixBuffer`) = dry input + outputs of chains with `recordOnCapture` on (muted chains excluded). Both the take recorder and the looper tap this mix, so a synth chain can be excluded from a guitar take. All chains on = identical to the old post-chain tap.
- **MIDI clock**: three independent ways to run the clock; it runs while **any** source wants it (`clockRunning` atomic, `updateClockRunState()` on the message thread + `refreshClockRunning()` per block). (1) The global toggle (`MidiClock.jsx` section) **free-runs** — if no transport step advanced `metronomePosition` in a block, the clock advances it itself, and it sounds the audible click (subject to the metronome toggle), so a drum machine can be driven without recording. (2) The **take recorder** (`takeMidiClock` toggle in the transport) and (3) the **looper** (`looperMidiClock` toggle) start the clock when their operation starts (count-in or capture) and stop it when it ends — no free-run. Toggling/starting/stopping sends Start/Stop **directly to the output device** (`sendDirectMidiStart`/`Stop`) because the standalone never forwards the host MIDI buffer to hardware; a start at actual-recording/looper-capture time re-syncs gear when the clock runs from another source (a take-driven clock starts at the count-in instead, so no mid-count-in restart). Recording/looper start/stop still queue pending Start/Stop for the host buffer. Per-section clocks are persisted (`takeMidiClock`/`looperMidiClock` state props, default off). **Take click**: `clickDuringTake` (default on) — when off, the click plays only during the count-in, never through the take.
- **Bus layout**: `isBusesLayoutSupported` accepts 1–8 input channels with matching output count (mono/stereo/quad/5.0/5.1/7.1/8ch); stereo is the preferred default. `recordBuffer`/snippets are channel-agnostic, so multi-channel captures produce multichannel WAVs.
- **Events**: chain lifecycle via `frontendAddChain`, `frontendRemoveChain`, `frontendRenameChain`, `frontendSetChainInputs`, `frontendSetChainRecord`, `frontendSetChainVolume`, `frontendSetChainMute`, `frontendSetChainMidiChannels`; slot ops (`frontendAddVst3` etc.) carry a `chain` field with the chain id. Transport extras: `frontendSetDryLevel` (Dry knob), `frontendSetClickParams` (click sound popover), `frontendSetSnippetColor` (library swatches). `backendVst3Chain` ships `inputChannels` + `chains: [{id, name, inputs, wantsMidi, midiChannels, recordOnCapture, volume, muted, slots}]` + `openEditors`; `backendTransport` ships `chainLevels: [{chain, level, peak}]` at 30 Hz.
- **Persistence**: `makeChainState` emits `{ chains: [...], nextChainId, blocklist, availablePlugins }`; `applyChainState` migrates the old `midiChain`/`audioChain` split format (→ two chains "MIDI Chain"/"Audio FX Chain") and the pre-split single `slots` format (→ one chain). `ensureUniqueChainIds` fixes missing/duplicate ids after restore. Absent `volume`/`muted`/`midiChannels` in old states default to 0 dB / unmuted / all channels. `applyChainState` suppresses `persistPluginChain` for its whole duration so a restore can never echo a partial state back into the properties file (the JUCE standalone calls `setStateInformation` at startup via `reloadPluginState`, and an unguarded echo used to wipe the saved chains). `loadSavedChainState()` picks whichever of the `pluginChains`/`pluginChain` keys actually holds chain content so a stale/corrupt newer key can't shadow the older valid one.
- **Deferred chain restore**: `setChainState` never instantiates plugins — saved slots are queued as `PendingSlot`s (path, bypass, state blob) and the processor's `timerCallback` (the `pendingPluginLoads`/`restoreActive` driver) loads them ONE per message-loop turn via `addPluginAsync`, applying bypass + saved state in the load callback. This is deliberate: synchronously instantiating several plugins in a row kept the message thread inside plugin code for hundreds of ms, so window messages plugins queue during their own setup got dispatched reentrantly and crashed some plugins (Neural DSP "X" amp sims died with a heap fault in the first instance's window proc). One slot per loop turn gives every plugin an idle gap. **Same-chain duplicates of the same .vst3 are rejected** (`hasPluginFile`) because two instances in one chain crash those plugins; the same plugin on *different* chains is fine. A `chainRestoreCrashed` properties-file marker self-heals: if a launch crashes mid-restore, the next launch skips state-blob restore (plugins load with defaults). Crash diagnostics: a `SetUnhandledExceptionFilter` writes `%APPDATA%\Retrokielto\crash-info.txt` (current restore op + module-offset backtrace) via `setCrashOp`/`getCrashOp`.
- Editor plugin windows: `editorWindows` keyed by chain id → slot index; `refreshChainEditorBindings()` rewires per-chain `onSlotRemoved` after every chain-list change.

## Skills
This project has four OpenCode skills configured:
- **blueprinter**: General project knowledge, architecture, conventions
- **webui-bridge**: React WebUI and C++ WebView2 bridge development
- **juce-audio**: JUCE audio plugin patterns specific to this project
- **hallmark**: Third-party anti-AI-slop design skill (from github.com/Nutlope/hallmark) for greenfield UI pages, audits, redesigns, and design extraction. Not BluePrinter-specific — triggers on UI/landing-page design requests.
