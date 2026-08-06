---
name: webui-bridge
description: Use ONLY when working with the BluePrinter React WebUI or the C++ WebView2 bridge. Covers bridge.js API (emit/subscribe), frontend/backend event naming, component patterns, Vite build, and CSS conventions.
---

# WebUI & Bridge Development

The BluePrinter UI is a React app hosted inside a JUCE WebView2 (`BluePrinterWebViewEditor`). Communication is bi-directional through `window.__JUCE__.backend`.

## Bridge API (`WebUI/src/bridge.js`)

```js
// Import path: components use "../bridge", files directly in src/ use "./bridge"
import { emit, subscribe, getBackend, getInitialData, FRONTEND_EVENTS, BACKEND_EVENTS } from "../bridge";

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
- `updateSnippet`, `deleteSnippet`, `detectSnippetKey`, `saveSnippet`, `saveLoop`, `revealSnippet`
- `chooseLibraryFolder`, `openLibraryFolder`, `refreshLibrary`, `getSnippets`
- `setMetronome`, `setBpm`, `setCountInBeats`, `setMidiClock`, `setMidiDevice`
- `setClickDuringCapture` (`{ enabled }`) — header-level, shared by the take recorder and the looper: false = click only during count-ins, silent through takes and loop captures
- Take review (pending take): `setTakePlayback` (`{ enabled }` — one-shot replay of the pending take, overwrites output, playback-volume-scaled), `saveTake` (writes the pending take to the library folder as a snippet + WAV/JSON, no dialog), `discardTake`. The backend ships `takePending`/`takeLength`/`takePlaying`/`takePosition`/`takePeaks` in the transport snapshot and `TakeReview.jsx` renders the panel. Any new capture (take or loop) invalidates the pending take.
- `setDryLevel` (`{ level }`, 0–1, the "Dry" knob — applied post-gain to the output and the record mix)
- `setClickParams` (`{ pitch, accentPitch, decay, volume, accentVolume, noise }`, the click-sound popover in the transport; see `resynthesizeClicks()` in `PluginProcessor.cpp` — two-tone 1000/1500 Hz click, the transport snapshot carries the same fields back)
- `setSnippetColor` (`{ id, color }` — a named color from `SNIPPET_COLORS` in `utils.js`; persisted in the snippet JSON sidecar as `color`)
- `renameTag` (`{ color, name }`) — user-editable name for one of the 8 colour tags; empty name resets to the built-in label. Persisted as a `tagNames` JSON object in the C++ properties file (`BluePrinterAudioProcessor::setTagName`) and shipped back to the UI inside every `snippets` snapshot and the `tagNames` initialisation blob; `App.jsx` keeps it in `tagNames` state and `SnippetList`/`SnippetCard` fall back to the built-in labels when a key is absent
- `setLooperRecording`, `setLooperPlaying`, `setLooperLooping`, `setLooperCountIn`, `setLoopCrop`, `clearLoop`, `saveLoop`
- The looper is **audio-only** (no MIDI event capture/quantize/lane — those were removed). Transport snapshots expose `looperRecording`, `looperPreRoll`, `looperPlaying`, `looperLooping`, `looperCountInBeats`, `looperCropStartBeats`, `looperCropEndBeats`, `audioLoopStart` / `audioLoopPosition` / `audioLoopLength`, and `audioLoopPeaks` (waveform buckets for the timeline, re-rendered via the shared `Waveform` component). Use `audioLoopPosition` / `audioLoopLength` for the visual timeline and playhead.
- `frontendSetLoopCrop` payloads are `{ startBeats, endBeats }` in whole beats (4 per bar); `frontendSetLooperCountIn` is `{ beats: 0..8 }`; `frontendSetLooperRecording` / `frontendSetLooperPlaying` / `frontendSetLooperLooping` are `{ enabled }`. The looper has **no per-section click or MIDI-clock toggles** — the header owns those (`setMetronome`, `setClickDuringCapture`, `setMidiClock`).
- `saveLoop` converts the captured (cropped) audio loop into a library snippet and opens the same WAV + JSON save dialog as the take recorder (`saveSnippetWithDialog`). There is no MIDI `.mid` save/load.
- `addVst3`, `removeVst3`, `moveVst3`, `setVst3Bypass`, `setVst3MidiPass`, `openVst3Editor`, `closeVst3Editor`
- `scanVst3Folder`, `getVst3Chain`, `blockVst3Plugin`, `unblockVst3Plugin`
- `addChain`, `removeChain`, `renameChain`, `setChainInputs`, `setChainRecord`, `setChainVolume`, `setChainMute`, `setChainMidiChannels`
- Chain snapshots (`backendVst3Chain`) ship a `chains` array: each entry is `{ id, name, inputs: [0..7], wantsMidi, midiChannels: [1..16], recordOnCapture, volume (dB), muted, pending, slots }` plus a top-level `inputChannels` count. `setVst3MidiPass` / `setChainInputs` / `setChainRecord` / `renameChain` / `setChainVolume` / `setChainMute` / `setChainMidiChannels` payloads are `{ chain: <chain id>, ... }` (`setChainInputs` carries `inputs: [0..7]`, `setChainMidiChannels` carries `channels: [1..16]`, `setChainVolume` carries `volume` in dB). There is no `CHAIN_IDS` constant — chain ids are generated by the backend ("chain0", "chain1", …).
- Live per-chain meters arrive on the 30 Hz `backendTransport` push as `chainLevels: [{ chain, level, peak }]`.

**Backend events** (C++ → React):
- `parameters` — `{ gain: number, playbackVolume: number }`
- `snippets` — Array of snippet objects OR `{ snippets: [...], libraryFolder, lastSaveError, tagNames }` (`tagNames` is `{ colorKey: userName }`, sent with every library snapshot)
- `transport` — Full transport state object, including `chainLevels`, plus `dryLevel`, `clickPitch`/`clickAccentPitch`/`clickDecay`/`clickVolume`/`clickAccentVolume`/`clickNoise`, `metronomeEnabled`, `clickDuringCapture`, `midiClockEnabled`, `takePending`/`takeLength`/`takePlaying`/`takePosition`/`takePeaks`, `midiOutputDevice`, and `midiOutputDeviceList`
- The **header-level MIDI clock** toggle (`HeaderControls.jsx`) free-runs whenever on: it sends Start + 24 ppqn to `midiOutputDevice` and takes / loop captures ride it (re-syncing with a Start at actual-recording/capture time). `setMidiClock`/`setMidiDevice` are wired from `App.jsx` into `HeaderControls`, alongside the master **Click** toggle (`setMetronome`) and the **Click: capture** toggle (`setClickDuringCapture`). There is no standalone MidiClock section anymore.
- `notify` — `{ message: string, level: "info"|"warn"|"error" }`
- `vst3Chain` — `{ chains: [{id, name, inputs, wantsMidi, midiChannels, recordOnCapture, volume, muted, pending, slots}], openEditors, plugins, folder, blocklist, inputChannels, restoring, restoreError }` — `pending` is the number of saved slots still loading for that chain; `restoring` is true while the deferred restore is in flight (see the splash pattern below); `restoreError` carries the last restore failure text
- `vst3ScanProgress` — `{ active, current, total, currentFile, folder }`
- `vst3LoadFailed` — Per-plugin load failure notification

**Chain selection**: VST3 slot events carry a `chain` field with the target chain's id (e.g. `"chain0"`). The backend falls back to the first chain when the field is absent. Never rely on a `"midiChain"`/`"audioChain"` literal — those exist only in legacy saved states, which `applyChainState` migrates on load.

## Component Patterns

- **App.jsx** is the root. It subscribes to all backend events in `useEffect` hooks (with cleanup) and passes state down as props.
- **Emit pattern**: `setState()` immediately (optimistic UI), then `emit()` to C++.
- **Initial data**: Read from `getInitialData()` via `useMemo` for default state values.
- **Explicit snapshot requests**: Fire `FRONTEND_EVENTS.getSnippets` and `FRONTEND_EVENTS.getVst3Chain` on mount to compensate for lost listener notifications during startup. `App.jsx` also emits `getVst3Chain` on mount (coalesces with `PluginChain`'s emit) so the splash learns the restore state early.
- **Startup splash** (`SplashScreen.jsx`): full-screen overlay (`showing` → `leaving` fade → `hidden`) driven by the first `vst3Chain` snapshot: it stays while `restoring` is true, holds a `SPLASH_MIN_MS` minimum display time, and re-appears if a later host state load starts another restore. Progress = `loaded / (slots.length + pending)` summed across chains; keep splash state in `App.jsx` (`splashPhase` + `mountedAt` ref + two `useEffect`s, one for the min-delay fade, one for the fade-out timer). Don't block the app on it — it's an overlay, and the app renders underneath.
- **Takes toolbar** (`SnippetList.jsx`): all filtering/sorting is frontend-only — a search input (matches name/comments/notes/key), a sort select (newest/oldest/name/longest/shortest), a key filter select (distinct detected keys + "no key"), and a wrap row of tag-colour filter chips (multi-select, incl. an untagged chip) with a Clear button when any filter is active. The "Tags" button opens a rename popover with one input per colour (`renameTag`). The section count shows `filtered / total` while filtering.

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
- Component-level styles use class names prefixed with the component name (e.g., `.transport-`, `.snippet-card-`, `.plugin-chain-`, `.midi-clock-`, `.bp-splash-` for the splash overlay).
- Layout uses flexbox. The app is a single-page vertical layout with `header`, `Transport`, `TakeReview`, `Looper`, `PluginChain`, `library-section`, and `footer`; the splash is a `position: fixed` overlay with a high `z-index`. The header owns the shared recording settings (Click, Click: capture, MIDI clock + device select) as `.metronome-toggle` pills in `.header-click-sound` / `.header-midi-clock` groups, which `flex-wrap` on narrow widths.
- Controls that reveal extra UI on demand must not push sibling controls around: absolutely position the revealed element below its anchor instead of adding it to the flex row (e.g. `.transport-gain .gain-popover`). The MIDI clock's device selector lives in the `MidiClock` section itself (always visible, not revealed on demand).
- `ErrorBoundary.jsx` wraps the snippet library rows in `App.jsx`; a crashing child must never blank the whole WebView (white screen). Keep new volatile components behind it.
- Respect `prefers-reduced-motion`: the splash's logo pulse and indeterminate bar sweep are killed in the existing reduced-motion block — add new cosmetic animations there too.

## Don't

- Don't hardcode event name strings — use `FRONTEND_EVENTS` / `BACKEND_EVENTS`.
- Don't call `emit()` inside the `subscribe` handler of the same round-trip event (e.g. `getSnippets`/`getVst3Chain` on mount) on every tick — fire explicit snapshot requests **once** on mount.
- Don't use class components — this codebase is functional components + hooks only.
- There are no ESLint/Prettier configs; match the surrounding style by hand (2-space indentation in JSX, `const` arrow functions for components).
