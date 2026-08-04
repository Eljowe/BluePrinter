---
name: blueprinter
description: Use for BluePrinter plugin codebase tasks. Covers architecture, WebView2 bridge events, flexible multi-chain design, audio and MIDI recording/playback transport, MIDI sequencing, snippet library, VST3 hosting, key detection, metronome, and MIDI clock.
---

# BluePrinter Plugin

Audio plugin (VST3 + Standalone) for recording guitar takes, saving WAV + JSON sidecar files, and hosting VST3 FX chains. Built with JUCE and a React/Vite WebView2 UI. Windows-only (WebView2 requirement).

## Architecture

```
Audio input -> Gain (APVTS param) -> +-> [chain 0] ----> sum -> + -> Metronome mix -> Output
                                    |  \-> [chain 1] -----/     |
                                    \-> [chain N] -------------/  Recording capture = dry + selected chains (when armed)
```

- **Flexible list of parallel VST3 chains**: the processor owns `std::vector<std::unique_ptr<PluginChain>> chains` (guarded by `chainLock`; the audio thread iterates a raw-pointer snapshot `blockChains`). Each chain selects its input channels (`ChainInputBits` mask: none/left/right/both), whether it receives the MIDI buffer (`PluginChain::wantsMidi`, `frontendSetVst3MidiPass`), and whether its output lands in captures (`recordOnCapture`, `frontendSetChainRecord`).
- **Recording** captures `recordingMixBuffer` = dry post-gain input + the outputs of chains with `recordOnCapture` on. Playback substitutes the recorded buffer in place of live input. All chains on = identical to the old post-chain tap.
- **Metronome**: Synthesized in `processBlock` as a two-tone percussive click (1000 Hz tick / 1500 Hz accent, attack ramp, noise transient, decaying envelope) via `resynthesizeClicks()` — the pitch/snap/volume parameters (`clickPitch`, `clickAccentPitch`, `clickDecay`, `clickVolume`, `clickAccentVolume`, `clickNoise`) are editable in the transport's "Click sound" popover (`frontendSetClickParams`) and persisted. BPM, count-in beats, and MIDI clock output are configurable. Note: metronome/BPM/count-in/MIDI-clock/click settings are **not** APVTS parameters — they live in standalone user state (`juce::PropertiesFile`) and `std::atomic` audio-thread flags, not in the APVTS. APVTS currently exposes only `Gain`.
- **MIDI**: The plugin accepts MIDI input and hands each MIDI-listening chain its own copy of the input buffer — no cross-chain MIDI flow, and the host's output MIDI stays raw input (+ clock events). There is no MIDI event recording/sequencing anymore — the looper is audio-only. MIDI-driven instruments are captured as audio.
- **MIDI clock**: the clock runs while **any** source wants it (`clockRunning` atomic; `updateClockRunState()` message thread, `refreshClockRunning()` audio thread). (1) The **global toggle** (its own `MidiClock.jsx` section) **free-runs** — if no transport step (count-in/recording/looper) advanced `metronomePosition` in a block, the clock advances it itself, so a drum machine runs without recording. The free-running clock also renders the audible metronome click (`renderMetronomeInBlock`, subject to the metronome toggle). (2) The **take recorder** (`takeMidiClock` toggle in the transport) and (3) the **looper** (`looperMidiClock` toggle) start the clock when their operation starts (count-in or capture) and stop it when it ends — no free-run. Toggling/starting/stopping sends Start/Stop **directly to the output device** (`sendDirectMidiStart`/`sendDirectMidiStop`, `midiOutputLock`) — the standalone never forwards the host MIDI buffer to hardware, so without the direct send a drum machine stays silent until a recording starts. A take/looper driven by another source re-syncs at actual-recording/capture time (`midiStartPending`, skipped for the clock's own source so no mid-count-in restart). `clickDuringTake` (default on) gates the click during actual recording; when off the click only plays during the count-in.
- **Audio looper**: The sequencer panel was replaced with a pure audio looper. It captures the record mix (dry + selected chains, so synth sounds and FX are baked into the loop), plays back audio-only, and supports click/count-in, crop start/end in whole bars, loop/one-shot, and saving the loop as a library snippet.
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
| `WebUI/src/bridge.js` | JavaScript bridge API: `emit()`, `subscribe()`, `FRONTEND_EVENTS`, `BACKEND_EVENTS` constants |
| `WebUI/src/App.jsx` | Main React app, state management, all backend event subscriptions |
| `WebUI/src/main.jsx` | React entry point (mounts `App`) |
| `WebUI/src/utils.js` | Shared JS helpers |
| `WebUI/src/styles.css` | Global app styles (component styles use prefixed class names) |
| `WebUI/src/components/` | React components: `Transport`, `MidiClock`, `SnippetList`, `SnippetCard`, `PluginChain`, `LevelMeter`, `Waveform`, `Notification`, `LibraryFolderRow`, `ErrorBoundary`, `controls`, `icons` |
| `CMakeLists.txt` | Build config (CMake + JUCE), VST3/Standalone targets, install rules |

## WebView2 Bridge Convention

All frontend↔backend communication uses named events via `window.__JUCE__.backend`. The React side imports constants from `bridge.js` (`FRONTEND_EVENTS`, `BACKEND_EVENTS`); the C++ side defines matching `static constexpr const char*` strings in `WebViewEditor.h`. The two must match exactly — never hardcode event strings.

Import path depends on location: files in `WebUI/src/` use `from "./bridge"`; components use `from "../bridge"`.

**Explicit snapshot requests** (`frontendGetSnippets`, `frontendGetVst3Chain`): The processor loads data before the editor is constructed, so initial `listener` callbacks are lost. The React app fires these events once on mount to get fresh snapshots.

For the full event listing and the add-a-new-event recipe, see the **webui-bridge** skill.

## VST3 Chain Management

- **Flexible chain list**: chains live in `std::vector<std::unique_ptr<PluginChain>> chains` on the processor (guarded by `chainLock`); ids are stable (`"chain0"`, `"chain1"`, … generated from `nextChainId`) and survive renames/reorders. New chains default to inputs both, wantsMidi on, all MIDI channels, record on, 0 dB, unmuted. There is no `CHAIN_IDS` constant anymore — the id is whatever the backend generated.
- **All mutations happen on the message thread**; `processBlock` runs on the audio thread.
- **Async plugin loading**: `addPlugin` has an async variant that runs `findAllTypesForFile` + `createPluginInstance` on a worker thread to avoid blocking the message thread with misbehaving plugins.
- **Blocklist**: `Vst3Library` maintains a blocklist of problematic plugins. Use `frontendBlockVst3Plugin` / `frontendUnblockVst3Plugin` to manage it.
- Chain events carry a `chain` field (the chain id) selecting which chain to operate on.
- **Chain lifecycle events**: `frontendAddChain` (`{ name?, inputs?, wantsMidi?, recordOnCapture? }`), `frontendRemoveChain`, `frontendRenameChain` (`{ chain, name }`), `frontendSetChainInputs` (`{ chain, inputs: [0..7] }`), `frontendSetChainRecord` (`{ chain, enabled }`), `frontendSetChainVolume` (`{ chain, volume }` dB), `frontendSetChainMute` (`{ chain, muted }`), `frontendSetChainMidiChannels` (`{ chain, channels: [1..16] }`). Slot ops (`frontendAddVst3` etc.) resolve the chain via `processor.getChainById(chain)`.
- **MIDI pass-through** (`frontendSetVst3MidiPass`, `{ chain, enabled }`): per-chain toggle for whether the chain's plugins receive the MIDI buffer. `PluginChain::processBlock` hands the plugins a local empty buffer when off. Each MIDI-listening chain gets its own copy of the input MIDI (`chainMidiScratch`), filtered by its channel mask (`midiChannelsMask`, channels 1–16, system messages always pass) into `chainMidiFiltered` — one chain's generated notes never leak into another. Serialized in `getChainState`/`setChainState` (`wantsMidi`, `midiChannels`), so it persists in plugin state and the user-state bundle; states saved before the toggles existed default to on / all channels. Note: per-chain MIDI **device** selection is not possible — JUCE's standalone wrapper auto-opens and merges all MIDI devices (`juce_StandaloneFilterWindow.h`), JUCE 8 `MidiMessage` carries no source ID, and hosts own device routing in VST3. The MIDI channel is the only per-message discriminator; per-device routing would require a custom standalone app main (`JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP`).
- **Input routing**: each chain copies its selected input channels from the pristine post-gain snapshot `chainInputBuffer` (never from the accumulating mix — chains can't hear each other) into the shared `chainScratchBuffer`; channels beyond the block's count stay silent ("Right" on a mono layout receives silence). Output is summed into the main buffer × the chain's volume gain, unless muted. Muted chains still run their plugins but contribute nothing. The input bus supports 1–8 channels (`isBusesLayoutSupported`), stereo preferred.
- **Recording selection**: `recordOnCapture` per chain feeds `recordingMixBuffer` = dry input + selected chains (muted excluded), tapped by both the take recorder and the looper. Zero selected chains records the dry input only. Multi-channel input produces multichannel WAVs.
- **Level meters**: each chain has public `outputLevel`/`outputPeak` atomics written per block post-volume (muted reads 0) via the shared `computeLevelsInto` helper; the editor pushes them at 30 Hz as `chainLevels` in the transport snapshot.
- **Persistence**: `makeChainState` emits `{ chains: [{id, name, inputs, midiChannels, recordOnCapture, volume, muted, wantsMidi, slots}], nextChainId, blocklist, availablePlugins }`; `applyChainState` migrates the old `midiChain`/`audioChain` split (→ two chains "MIDI Chain"/"Audio FX Chain") and the pre-split single `slots` format (→ one chain). `ensureUniqueChainIds` fixes missing/duplicate ids and bumps `nextChainId` past the highest id in use. Absent `volume`/`muted`/`midiChannels` in old states default to 0 dB / unmuted / all channels.
- **Editor windows**: `WebViewEditor::editorWindows` is keyed by chain id → slot index; `refreshChainEditorBindings()` rewires per-chain `onSlotRemoved` after every chain-list change so removed slots/chains always close their windows.

## Audio Looper

- CMake must set `NEEDS_MIDI_INPUT TRUE`; otherwise external keyboards and drum machines cannot feed the plugin's MIDI chains.
- The looper is **audio-only**: no MIDI event capture, no quantization, no note lane. The MIDI event machinery (`midiSequence`, quantization, note flushing, MidiLane) was deliberately removed. MIDI input still drives the MIDI-listening chains' plugins live in every block.
- Capture taps the **record mix** (`recordingMixBuffer` — dry input + chains with `recordOnCapture` on, before the click is mixed) into the shared preallocated `recordBuffer`, so the loop contains the actual sound from the selected chains only. Playback `addFrom`s the loop after the chains too — the loop audio is already processed, so re-running it through the chains would double-process it.
- Looper controls are message-thread requests exposed through `WebViewEditor.h`, `WebViewEditor.cpp`, and `WebUI/src/bridge.js`:
  - `frontendSetLooperRecording` (starts capture, with count-in if click is on)
  - `frontendSetLooperPlaying` (audio-only loop playback; also drives MIDI Start/Stop when clock output is on)
  - `frontendSetLooperLooping` (loop vs one-shot)
  - `frontendSetLooperClick` / `frontendSetLooperCountIn` (per-looper click + count-in beats, off the shared metronome clock; defaults click on, 4 beats)
  - `frontendSetLoopCrop` (trim start/end in whole bars — see below)
  - `frontendClearLoop`
  - `frontendSaveLoop` (converts the cropped loop into a library snippet via `addLoopSnippet()`)
- Transport snapshots expose `looperRecording`, `looperPreRoll`, `looperPlaying`, `looperLooping`, `looperClickEnabled`, `looperCountInBeats`, `looperCropStartBars`, `looperCropEndBars`, `audioLoopStart`, `audioLoopPosition`, `audioLoopLength`, and `audioLoopPeaks` (downsampled waveform of the cropped loop, rebuilt by `refreshLooperPeaks()` on the message thread after capture/trim/crop and rendered in the timeline). The UI derives the playhead from `audioLoopPosition` / `audioLoopLength`.
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
