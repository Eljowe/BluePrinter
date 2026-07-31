---
name: webui-bridge
description: Use ONLY when working with the BluePrinter React WebUI or the C++ WebView2 bridge. Covers bridge.js API (emit/subscribe), frontend/backend event naming, component patterns, Vite build, and CSS conventions.
---

# WebUI & Bridge Development

The BluePrinter UI is a React app hosted inside a JUCE WebView2 (`BluePrinterWebViewEditor`). Communication is bi-directional through `window.__JUCE__.backend`.

## Bridge API (`WebUI/src/bridge.js`)

```js
// Import path: components use "../bridge", files directly in src/ use "./bridge"
import { emit, subscribe, getBackend, getInitialData, CHAIN_IDS, FRONTEND_EVENTS, BACKEND_EVENTS } from "../bridge";

// Get the JUCE backend handle
const backend = getBackend();  // window.__JUCE__?.backend

// Read the initialization blob injected by JUCE
const init = getInitialData(); // window.__JUCE__?.initialisationData ?? {}

// Send an event to C++
emit(FRONTEND_EVENTS.someEvent, { key: value });

// Subscribe to C++ events. Returns an unsubscribe function.
const unsub = subscribe(BACKEND_EVENTS.someEvent, (payload) => { ... });
// Call unsub() on cleanup (useEffect return)
```

## Event Constants

All frontend events are in `FRONTEND_EVENTS`, backend events in `BACKEND_EVENTS`. The C++ side defines matching strings in `WebViewEditor.h` as `static constexpr const char*`. Never hardcode event strings — always use the constants.

**Frontend events** (React → C++):
- `setParameter`, `startRecording`, `stopRecording`, `startPlayback`, `stopPlayback`
- `updateSnippet`, `deleteSnippet`, `detectSnippetKey`, `saveSnippet`, `revealSnippet`
- `chooseLibraryFolder`, `openLibraryFolder`, `refreshLibrary`, `getSnippets`
- `setMetronome`, `setBpm`, `setCountInBeats`, `setMidiClock`, `setMidiDevice`
- `setMidiSequencerRecording`, `setMidiSequencerPlaying`, `setMidiSequencerLooping`, `clearMidiSequence`
- Sequencer transport snapshots include `midiSequencerEventCount`, `midiSequencerPosition`, and `midiSequencerLength`; the UI should derive the playhead from these values.
- The combined audio/MIDI looper also exposes `audioLoopPosition` and `audioLoopLength`. Use these for the visual timeline and playhead because audio is the shared loop duration.
- MIDI sequencer bridge events include `frontendSaveMidiSequence`, `frontendLoadMidiSequence`, and `frontendSetMidiQuantization`. File chooser/load work stays on the message thread; quantization payloads use `{ division: 0|4|8|16|32 }`.
- Transport snapshots expose `midiQuantizationDivision` so the frontend selector remains synchronized with processor state.
- Transport snapshots also expose `midiEvents`; each event contains `position`, `note`, and `velocity`. `MidiLane.jsx` renders these as normalized pitch/time ticks and uses `audioLoopPosition` / `audioLoopLength` for the backend-driven playhead.
- `MidiLane` is memoized and must not use decorative keyframe animation for transport movement. The playhead position comes directly from backend snapshots.
- `addVst3`, `removeVst3`, `moveVst3`, `setVst3Bypass`, `openVst3Editor`, `closeVst3Editor`
- `scanVst3Folder`, `getVst3Chain`, `blockVst3Plugin`, `unblockVst3Plugin`

**Backend events** (C++ → React):
- `parameters` — `{ gain: number }`
- `snippets` — Array of snippet objects OR `{ snippets: [...], libraryFolder, lastSaveError }`
- `transport` — Full transport state object
- `notify` — `{ message: string, level: "info"|"warn"|"error" }`
- `vst3Chain` — `{ midiChain, audioChain, openEditors, plugins, folder }`
- `vst3ScanProgress` — `{ active, current, total, currentFile, folder }`
- `vst3LoadFailed` — Per-plugin load failure notification

**Chain selection**: `CHAIN_IDS.midi` = `"midiChain"`, `CHAIN_IDS.audio` = `"audioChain"`. Pass as the `chain` field in VST3 events.

## Component Patterns

- **App.jsx** is the root. It subscribes to all backend events in `useEffect` hooks (with cleanup) and passes state down as props.
- **Emit pattern**: `setState()` immediately (optimistic UI), then `emit()` to C++.
- **Initial data**: Read from `getInitialData()` via `useMemo` for default state values.
- **Explicit snapshot requests**: Fire `FRONTEND_EVENTS.getSnippets` and `FRONTEND_EVENTS.getVst3Chain` on mount to compensate for lost listener notifications during startup.

## Vite Build

```bash
cd WebUI
npm install
npm run build    # outputs to WebUI/dist/
```

Config in `vite.config.js`: React plugin, `base: "./"` for relative paths (required since the app is served from a local file path, not a web server). Build output at `WebUI/dist/`.

The C++ side loads `WebUI/dist/index.html` via `findLocalWebUiDistIndex()` in `WebViewEditor.cpp`, which walks up from the executable directory looking for a `WebUI/dist/` directory.

## Adding a New Event

1. Add `static constexpr const char* frontend/backendNewEvent` to `WebViewEditor.h`.
2. Register the event listener in `WebViewEditor` constructor.
3. Add the event constant to `bridge.js` (`FRONTEND_EVENTS` or `BACKEND_EVENTS`).
4. Emit/subscribe in the React component.

## CSS Conventions

- Global styles in `WebUI/src/styles.css`.
- Component-level styles use class names prefixed with the component name (e.g., `.transport-`, `.snippet-card-`, `.plugin-chain-`).
- Layout uses flexbox. The app is a single-page vertical layout with `header`, `Transport`, `PluginChain`, `library-section`, and `footer`.

## Don't

- Don't hardcode event name strings — use `FRONTEND_EVENTS` / `BACKEND_EVENTS` / `CHAIN_IDS`.
- Don't call `emit()` inside the `subscribe` handler of the same round-trip event (e.g. `getSnippets`/`getVst3Chain` on mount) on every tick — fire explicit snapshot requests **once** on mount.
- Don't use class components — this codebase is functional components + hooks only.
- There are no ESLint/Prettier configs; match the surrounding style by hand (2-space indentation in JSX, `const` arrow functions for components).
