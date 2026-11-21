---
name: blueprinter
description: Project map for BluePrinter tasks — architecture overview, module responsibilities, key-file map, and where the deeper docs live. Load first on any BluePrinter task, then load the specialist skill for your work: webui-bridge for React WebUI / WebView2 bridge code, juce-audio for C++ JUCE audio code.
---

# BluePrinter Plugin

Audio plugin (VST3 + Standalone; AU on macOS) for recording guitar takes, saving WAV + JSON sidecar files, and hosting VST3 FX chains. Built with JUCE and a React/Vite UI hosted in WebView2 (Windows), WKWebView (macOS) or WebKitGTK (Linux). Cross-platform (ADR-0006).

## Architecture

```
Audio input -> Input trim (APVTS Gain, dB) -> +-> [chain 0] ----> sum -> + -> Metronome mix -> Output
                                    |  \-> [chain 1] -----/     |
                                    \-> [chain N] -------------/  Recording capture = dry + selected chains (when armed)
```

- **Processor** (`Source/PluginProcessor.h/.cpp`): owns all state — APVTS (`Gain` → Input, `PlaybackVolume` → Output, both dB), the chain list, takes/looper recording + playback, metronome, MIDI clock, persistence. The editor is a listener/view; the frontend mirrors state.
- **UI bridge** (`Source/WebViewEditor.h/.cpp`): WebView2 editor, all frontend↔backend events. The React side never talks to C++ directly except through `bridge.js`.
- **React UI** (`WebUI/src/`): `App.jsx` subscribes to backend events and passes state down; components emit mutations.
- **Chains**: parallel VST3 FX chains in `std::vector<std::shared_ptr<PluginChain>>` (guarded by `chainLock`; the audio thread refreshes a `shared_ptr` snapshot under `tryEnter()`, reusing the previous block's on contention, and removed chains/slots are retired and reclaimed after two successful snapshot refills). Stable ids, per-chain input mask / MIDI / record / volume / mute. Full model: `docs/architecture/audio-path.md`.
- **Recording**: takes + looper share `recordBuffer` as live capture scratch, capture the record mix (dry + `recordOnCapture` chains), never capture simultaneously. Each stopped take is copied into its own buffer and appended to a bounded stack (8 / 256 MB) in `TakeRecorder` (TakeReview → select/audition/save/delete); Dub layers onto the selected take. Full model: `docs/architecture/audio-path.md`.
- **MIDI clock + click**: header-level toggles, Start/Stop queued to a lock-free `MidiSendQueue` and sent to the output device by `MidiClockOutput`'s sender thread, click gating. Full model: `docs/architecture/audio-path.md`.
- **Deferred restore**: saved chains load one plugin per message-loop turn; persist is gated while restoring. Full model: `docs/architecture/chain-restore.md`.

## Key Files

| File | Responsibility |
|------|---------------|
| `Source/PluginProcessor.h/.cpp` | Audio processor, APVTS parameters, audio recording/playback state machine, audio looper, metronome, MIDI clock, state persistence |
| `Source/WebViewEditor.h/.cpp` | WebView2-based editor, bi-directional C++↔JS bridge, all frontend/backend event dispatch |
| `Source/PluginEditor.h/.cpp` | Fallback native editor (used when WebView2 unavailable) |
| `Source/PluginChain.h/.cpp` | VST3 FX chain — owns `ChainSlot` list, serial audio processing, async plugin loading |
| `Source/SnippetLibrary.h/.cpp` | Thread-safe snippet CRUD with mutex, WAV save/load, peak computation. User tag names for the colour tags persist in the processor's properties file (`tagNames` JSON, `setTagName`/`getTagNames`), not per-snippet — snippets only carry the colour key. |
| `Source/Vst3Library.h/.cpp` | VST3 folder scanning, blocklist, `describeVst3File` / `describeVst3FileAsync` |
| `Source/KeyDetector.h/.cpp` | FFT-based musical key detection (Krumhansl-Schmuckler profile correlation) |
| `WebUI/src/bridge.js` | JavaScript bridge API: `emit()`, `subscribe()`, `FRONTEND_EVENTS`, `BACKEND_EVENTS` constants |
| `WebUI/src/App.jsx` | Main React app, state management, all backend event subscriptions |
| `WebUI/src/main.jsx` | React entry point (mounts `App`) |
| `WebUI/src/utils.js` | Shared JS helpers |
| `WebUI/src/styles.css` | Global app styles (component styles use prefixed class names) |
| `WebUI/src/components/` | React components: `Transport`, `HeaderControls`, `Looper`, `TakeReview`, `SplashScreen`, `SnippetList`, `SnippetCard`, `PluginChain`, `LevelMeter`, `Waveform`, `Notification`, `LibraryFolderRow`, `ErrorBoundary`, `controls`, `icons` |
| `CMakeLists.txt` | Build config (CMake + JUCE), VST3/Standalone targets, install rules |

## Where the deeper docs live

- **React WebUI / WebView2 bridge** (events, payloads, component patterns, CSS, Vite build, add-a-new-event recipe): load the **webui-bridge** skill.
- **C++ JUCE audio patterns** (`processBlock` order, APVTS, threading, plugin chains, snippets, adding a parameter): load the **juce-audio** skill.
- **Always-loaded rules, map and per-area summaries** (chains ownership, persist/restore rules, event naming): [`AGENTS.md`](../../../AGENTS.md).
- **Implementation mechanics and incident history** (load on demand): [`docs/architecture/`](../../../docs/architecture/README.md) — `audio-path.md` (chains, PDC, capture, clock), `chain-restore.md` (restore, self-heal, presets), `webui.md` (shell, bridge payloads, scaling, songs), `build-and-ci.md` (build, CI, release).
- **Coding conventions** (C++/React/threading/formatting, no linters): AGENTS.md "Coding Conventions".
- **Build commands**: AGENTS.md "Commands"; WebUI build in the webui-bridge skill; full build/CI/release in `docs/architecture/build-and-ci.md`.
