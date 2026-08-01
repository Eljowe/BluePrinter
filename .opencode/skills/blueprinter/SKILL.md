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
- **MIDI sequencer**: The plugin accepts MIDI input and can record incoming `juce::MidiMessage` events against the transport sample position. Recorded events are replayed through the existing `midiChain`, with loop playback controlled by atomic runtime state. The current implementation is a bounded event looper, not yet a persistent clip library or piano-roll editor.
- **MIDI looper transport**: Sequencer recording uses the transport sample clock and can share the count-in/metronome workflow. Playback advances a loop position and can issue MIDI Start/Stop through the existing MIDI clock path. Stopping playback must emit note-offs for tracked active notes.
- **Audio + MIDI looper**: The sequencer panel is evolving into a synchronized looper. It captures guitar/audio into the preallocated recording buffer while capturing MIDI events, then plays both from a shared loop transport. This is separate from the original audio snippet recording block; do not change the snippet recorder's controls or finalization behavior when extending the looper.
- **Looper boundary processing**: Audio loop playback uses a small precomputed crossfade window at the wrap point to reduce clicks. Loop capture stop trims the captured length to the nearest full 4/4 bar using BPM/sample rate, with a beat-length fallback, and applies the same length to MIDI timing.

## Key Files

| File | Responsibility |
|------|---------------|
| `Source/PluginProcessor.h/.cpp` | Audio processor, APVTS parameters, audio recording/playback state machine, MIDI sequencer, metronome, MIDI clock, state persistence |
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

## MIDI Sequencer

- CMake must set `NEEDS_MIDI_INPUT TRUE`; otherwise external keyboards and drum machines cannot feed the plugin.
- MIDI recording and playback are handled in `PluginProcessor::processBlock`, before the MIDI VST3 chain processes the shared MIDI buffer.
- Recorded events use a preallocated bounded `std::vector` to avoid audio-thread allocation. Do not replace this with an unbounded container or a lock in `processBlock`.
- Sequencer controls are message-thread requests exposed through `WebViewEditor.h`, `WebViewEditor.cpp`, and `WebUI/src/bridge.js`:
  - `frontendSetMidiSequencerRecording`
  - `frontendSetMidiSequencerPlaying`
  - `frontendSetMidiSequencerLooping`
  - `frontendClearMidiSequence`
  - `frontendSaveLoop` (converts the captured audio loop into a library snippet via `addLoopSnippet()`)
  - `frontendSetMidiQuantization`
- Transport snapshots expose `midiSequencerRecording`, `midiSequencerPlaying`, `midiSequencerLooping`, and `midiSequencerEventCount`.
- The current loop is sample-position based and follows the existing transport clock. It is intended for testing drum machines, MIDI keyboards, and MIDI instruments alongside guitar recording.
- Current limitations: no piano-roll editing, per-track routing, or note cleanup on loop boundaries. MIDI sequence persistence (JSON in plugin state), clip/loop naming via snippet saving, and a quantization UI all exist — do not assume the looper lacks them.
- Filter realtime MIDI clock, Start, Continue, and Stop messages while recording; they are transport signals, not musical sequence events.
- The UI playhead uses backend-reported `midiSequencerPosition` and `midiSequencerLength`; do not replace it with a decorative CSS animation. The event lane uses the same normalized loop width.
- MIDI event storage is fixed/preallocated (`maxMidiSequenceEvents`) and event count/position/length are published atomically. Do not use `std::vector::push_back()` or message-thread mutation of storage read by `processBlock`.
- The audio loop reuses the prepared `recordBuffer` and reports `audioLoopPosition` / `audioLoopLength` in the transport snapshot. The frontend derives the combined loop playhead from the audio loop timing so MIDI-only event density does not distort the timeline.
- Loop controls are currently exposed through the existing sequencer events: record/stop capture, play/stop loop, loop toggle, clear, and save loop. There is no MIDI `.mid` export — the loop's save action converts the captured audio into a library snippet through `BluePrinterAudioProcessor::addLoopSnippet()`, then opens the same WAV + JSON sidecar dialog the recording block uses (`saveSnippetWithDialog`).
- MIDI sequence state is serialized as JSON inside plugin state under `midiSequence`, with event positions and explicit MIDI byte arrays. The JSON loader restores only into the fixed preallocated event array and is message-thread only.
- MIDI quantization is non-destructive at capture time: `midiQuantizationDivision` accepts `0` (off), `4` (quarter), `8` (eighth), `16` (sixteenth), or `32` (thirty-second). Grid spacing is derived from current BPM and sample rate.
- Active sequencer notes are tracked by channel/note on the audio thread. Flush note-offs when playback transitions from active to stopped and at loop wrap boundaries to prevent hanging instrument voices.
- MIDI event snapshots are exposed in transport as `midiEvents` containing note-on positions, note numbers, and velocity for the visual lane. These are message-thread snapshot data; never build them from inside `processBlock`.

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
