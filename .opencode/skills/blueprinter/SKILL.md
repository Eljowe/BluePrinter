---
name: blueprinter
description: Use for BluePrinter plugin codebase tasks. Covers architecture, WebView2 bridge events, dual-chain design (midiChain/audioChain), audio and MIDI recording/playback transport, MIDI sequencing, snippet library, VST3 hosting, key detection, metronome, and MIDI clock.
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
- **MIDI chain**: The plugin accepts MIDI input and passes it through the `midiChain` VST3 plugins live. There is no MIDI event recording/sequencing anymore — the looper is audio-only. MIDI-driven instruments are captured as audio.
- **Audio looper**: The sequencer panel was replaced with a pure audio looper. It captures the **post-chain** signal (so synth sounds and FX are baked into the loop — capturing pre-chain produced silence for MIDI-driven sounds), plays back audio-only, and supports click/count-in, crop start/end in whole bars, loop/one-shot, and saving the loop as a library snippet.
- **Looper boundary processing**: Audio loop playback uses a small precomputed crossfade window at the wrap point. Loop capture stop trims the captured length to the nearest full 4/4 bar using BPM/sample rate, with a beat-length fallback.

## Key Files

| File | Responsibility |
|------|---------------|
| `Source/PluginProcessor.h/.cpp` | Audio processor, APVTS parameters, audio recording/playback state machine, audio looper, metronome, MIDI clock, state persistence |
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

## Audio Looper

- CMake must set `NEEDS_MIDI_INPUT TRUE`; otherwise external keyboards and drum machines cannot feed the plugin's MIDI chain.
- The looper is **audio-only**: no MIDI event capture, no quantization, no note lane. The MIDI event machinery (`midiSequence`, quantization, note flushing, MidiLane) was deliberately removed. MIDI input still drives the `midiChain` plugins live in every block.
- Capture taps the **post-chain** buffer (after `midiChain` + `audioChain`, before the click is mixed) into the shared preallocated `recordBuffer`, so the loop contains the actual sound. Playback `addFrom`s the loop after the chains too — the loop audio is already processed, so re-running it through the chains would double-process it.
- Looper controls are message-thread requests exposed through `WebViewEditor.h`, `WebViewEditor.cpp`, and `WebUI/src/bridge.js`:
  - `frontendSetLooperRecording` (starts capture, with count-in if click is on)
  - `frontendSetLooperPlaying` (audio-only loop playback; also drives MIDI Start/Stop when clock output is on)
  - `frontendSetLooperLooping` (loop vs one-shot)
  - `frontendSetLooperClick` / `frontendSetLooperCountIn` (per-looper click + count-in beats, off the shared metronome clock; defaults click on, 4 beats)
  - `frontendSetLoopCrop` (trim start/end in whole bars — see below)
  - `frontendClearLoop`
  - `frontendSaveLoop` (converts the cropped loop into a library snippet via `addLoopSnippet()`)
- Transport snapshots expose `looperRecording`, `looperPreRoll`, `looperPlaying`, `looperLooping`, `looperClickEnabled`, `looperCountInBeats`, `looperCropStartBars`, `looperCropEndBars`, `audioLoopStart`, `audioLoopPosition`, and `audioLoopLength`. The UI derives the playhead from `audioLoopPosition` / `audioLoopLength`.
- Loop capture reuses `recordBuffer` (shared with the take recorder — `setLooperRecording` and `startRecording` preempt each other so they never capture simultaneously). The write position is `audioLoopLength` while capturing; the frontend shows live capture progress.
- **Crop**: `audioLoopStart` (samples skipped from the captured region's start) + `audioLoopLength` define the audible window; playback and `addLoopSnippet` read `[audioLoopStart, audioLoopStart + audioLoopLength)`. The UI sends crop in whole bars (derived from BPM/sample rate); `setLoopCrop` clamps so start+end never eats the whole loop and keeps the playhead inside the window.
- **Count-in**: mirrors the take-recorder pre-roll with its own `looperPreRollActive` flag. The click renders post-chain (before the capture tap would matter) and never lands in the loop because the tap runs before the click is mixed. Capture start fires MIDI Start (clock output enabled) so external gear syncs.
- Stop capture trims to the nearest full 4/4 bar (beat-length fallback) via `trimLooperToMusicalGrid`, which also resets any crop.
- MIDI clock output, metronome, BPM, and count-in for the take recorder are untouched transport features.

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
