---
name: blueprinter
description: Use for BluePrinter plugin codebase tasks. Covers architecture, WebView2 bridge events, dual-chain design (midiChain/audioChain), recording/playback transport, snippet library, VST3 hosting, key detection, metronome, and MIDI clock.
---

# BluePrinter Plugin

Audio plugin (VST3 + Standalone) for recording guitar takes, saving WAV + JSON sidecar files, and hosting VST3 FX chains. Built with JUCE and a React/Vite WebView2 UI. Windows-only (WebView2 requirement).

## Architecture

```
Audio input -> Gain (APVTS param) -> [midiChain] -> [audioChain] -> Metronome mix -> Output
                                          |                              |
                                   Recording capture              Metronome insert
                                   (when armed)
```

- **Two parallel VST3 chains**: `midiChain` runs first (arpeggiators, chord generators, MIDI instruments), `audioChain` runs second (amp sims, EQ, reverb). Both are `PluginChain` instances owned by the processor.
- **Recording** captures audio between the gain stage and the chains. Playback substitutes the recorded buffer in place of live input.
- **Metronome**: Synthesized in `processBlock` as a 50 ms percussive click (800 Hz fundamental + harmonics, decaying envelope), not a pure sine. BPM, count-in beats, and MIDI clock output are configurable. Note: metronome/BPM/count-in/MIDI-clock settings are **not** APVTS parameters — they live in standalone user state (`juce::PropertiesFile`) and `std::atomic` audio-thread flags, not in the APVTS. APVTS currently exposes only `Gain`.

## Key Files

| File | Responsibility |
|------|---------------|
| `Source/PluginProcessor.h/.cpp` | Audio processor, APVTS parameters, recording/playback state machine, metronome, MIDI clock, state persistence |
| `Source/WebViewEditor.h/.cpp` | WebView2-based editor, bi-directional C++↔JS bridge, all frontend/backend event dispatch |
| `Source/PluginEditor.h/.cpp` | Fallback native editor (used when WebView2 unavailable) |
| `Source/PluginChain.h/.cpp` | VST3 FX chain — owns `ChainSlot` list, serial audio processing, async plugin loading |
| `Source/SnippetLibrary.h/.cpp` | Thread-safe snippet CRUD with mutex, WAV save/load, peak computation |
| `Source/Vst3Library.h/.cpp` | VST3 folder scanning, blocklist, `describeVst3File` / `describeVst3FileAsync` |
| `Source/KeyDetector.h/.cpp` | FFT-based musical key detection (Krumhansl-Schmuckler profile correlation) |
| `WebUI/src/bridge.js` | JavaScript bridge API: `emit()`, `subscribe()`, `FRONTEND_EVENTS`, `BACKEND_EVENTS`, `CHAIN_IDS` constants |
| `WebUI/src/App.jsx` | Main React app, state management, all backend event subscriptions |
| `WebUI/src/main.jsx` | React entry point (mounts `App`) |
| `WebUI/src/utils.js` | Shared JS helpers |
| `WebUI/src/styles.css` | Global app styles (component styles use prefixed class names) |
| `WebUI/src/components/` | React components: `Transport`, `SnippetList`, `SnippetCard`, `PluginChain`, `LevelMeter`, `Waveform`, `Notification`, `LibraryFolderRow`, `controls`, `icons` |
| `CMakeLists.txt` | Build config (CMake + JUCE), VST3/Standalone targets, install rules |

## WebView2 Bridge Convention

All frontend↔backend communication uses named events via `window.__JUCE__.backend`. The React side imports constants from `bridge.js` (`FRONTEND_EVENTS`, `BACKEND_EVENTS`, `CHAIN_IDS`); the C++ side defines matching `static constexpr const char*` strings in `WebViewEditor.h`. The two must match exactly — never hardcode event strings.

Import path depends on location: files in `WebUI/src/` use `from "./bridge"`; components use `from "../bridge"`.

**Explicit snapshot requests** (`frontendGetSnippets`, `frontendGetVst3Chain`): The processor loads data before the editor is constructed, so initial `listener` callbacks are lost. The React app fires these events once on mount to get fresh snapshots.

For the full event listing and the add-a-new-event recipe, see the **webui-bridge** skill.

## VST3 Chain Management

- **Two chains**: `"midiChain"` and `"audioChain"` (see `bridge.js` CHAIN_IDS).
- **All mutations happen on the message thread**; `processBlock` runs on the audio thread.
- **Async plugin loading**: `addPlugin` has an async variant that runs `findAllTypesForFile` + `createPluginInstance` on a worker thread to avoid blocking the message thread with misbehaving plugins.
- **Blocklist**: `Vst3Library` maintains a blocklist of problematic plugins. Use `frontendBlockVst3Plugin` / `frontendUnblockVst3Plugin` to manage it.
- Chain events carry a `chain` field selecting which chain to operate on.

## Coding Conventions

- **C++**: `#pragma once`, `#include <JuceHeader.h>`, JUCE coding style (member initializer lists, `juce::` namespace).
- **Threading**: Audio thread vs message thread. Atomic flags for audio thread state checks. AsyncUpdater for deferred UI updates from background threads.
- **React**: Functional components with hooks, `emit()` for mutations, `subscribe()` in `useEffect` with cleanup. `getInitialData()` for reading the initialization blob.
- **State**: No global variables. Processor owns all state. Editor is a listener/view. Frontend mirrors state in `useState` hooks.
- **Formatting**: Consistent brace style, 4-space indentation in C++, 2-space in JSX.
- **No configured linters/formatters**: there is no `.clang-format`, ESLint, or Prettier config in the repo. Match the style of surrounding code by hand. See AGENTS.md "Commands" for what can be run.

## Don't

- Don't allocate, lock, or call UI APIs from `processBlock`.
- Don't hardcode event name strings — use the `FRONTEND_EVENTS` / `BACKEND_EVENTS` constants (JS) and the `static constexpr const char*` in `WebViewEditor.h` (C++).
- Don't emit a `getSnippets` / `getVst3Chain`-style round-trip from inside that event's own `subscribe` handler in the same tick (infinite loop).
- Don't add metronome / BPM / count-in / MIDI-clock settings to APVTS — they are standalone user state, not parameters.
