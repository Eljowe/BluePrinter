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

The two lists are **machine-checked**: `Tests/check-bridge-events.mjs` (registered as the `BridgeEventParity` CTest case, and run in CI) compares every `frontend*`/`backend*` string in `WebViewEditor.h` against `bridge.js` and fails with a readable one-sided diff. Renaming an event means changing both sides in the same commit.

**Frontend events** (React → C++):
- `setParameter`, `startRecording`, `stopRecording`, `startPlayback`, `stopPlayback`
- `updateSnippet`, `deleteSnippet`, `detectSnippetKey`, `saveSnippet`, `saveLoop`, `revealSnippet`
- `chooseLibraryFolder`, `openLibraryFolder`, `refreshLibrary`, `getSnippets`
- `setMetronome`, `setBpm`, `setCountInBeats`, `setMidiClock`, `setMidiClockOnRecord` (`{ enabled }` — restrict the MIDI clock to take/loop captures instead of free-running), `setMidiDevice`
- `setClickDuringCapture` (`{ enabled }`) — header-level, shared by the take recorder and the looper: false = click only during count-ins, silent through takes and loop captures
- Take review (pending take): `setTakePlayback` (`{ enabled }` — one-shot replay of the pending take, overwrites output, scaled by the master Output), `saveTake` (writes the pending take to the library folder as a snippet + WAV/JSON, no dialog), `discardTake`. The backend ships `takePending`/`takeLength`/`takePlaying`/`takePosition`/`takePeaks` in the transport snapshot and `TakeReview.jsx` renders the panel. Any new capture (take or loop) invalidates the pending take.
- `setLoopLevel` (`{ level }`, dB −60..+12, the looper's "Loop" monitor knob — scales loop playback only, never the capture)
- `setOverdubLevel` (`{ level }`, dB −60..0, the looper's "Dub" knob — trims each new overdub layer before `mixOverdubLayer` sums it into the loop; 0 dB is a no-op)
- `setDryLevel` (`{ level }`, dB −60..0, the monitor deck's "Dry" knob — scales the direct dry pass-through in **both** the monitor and the capture; at −60 dB only the chains are heard and printed, the chains always get the full input)
- `resetClip` (`{ target }` — clear a latched clip indicator; target is `"input" | "record" | "output" | "loop" | "all"`). The transport snapshot ships `inputClipped`/`recordClipped`/`outputClipped`/`loopClipped` plus the matching `recordLevel`/`recordPeak`, `outputLevel`/`outputPeak`, and `loopPlayLevel`/`loopPlayPeak` meter values. `LevelMeter.jsx` renders the dBFS −60..0 scale (headroom zone, peak-hold marker) and the click-to-reset clip LED; the monitor deck (`HeaderControls.jsx`) shows IN/REC/OUT, the looper a Loop meter.
- `setClickParams` (`{ pitch, accentPitch, decay, volume, accentVolume, noise }`, the click-sound popover in the header; see `resynthesizeClicks()` in `PluginProcessor.cpp` — two-tone 1000/1500 Hz click, the transport snapshot carries the same fields back)
- `setSnippetColor` (`{ id, color }` — a named color from `SNIPPET_COLORS` in `utils.js`; persisted in the snippet JSON sidecar as `color`)
- `setSnippetGain` (`{ id, gainDb }` — non-destructive playback trim, −24..+24 dB) and `normalizeSnippet` (`{ id }` — set the trim so the peak lands at −1 dBFS). Both persist `gainDb` in the JSON sidecar; the backend debounces the write + snapshot by 500 ms since the `SnippetCard` Gain knob emits per pointer move. `snippetToVar` ships `gainDb` on every snippet; `SnippetCard.jsx` expanded details have the Gain knob + Normalize button.
- `renameTag` (`{ color, name }`) — user-editable name for one of the 8 colour tags; empty name resets to the built-in label. Persisted as a `tagNames` JSON object in the C++ properties file (`BluePrinterAudioProcessor::setTagName`) and shipped back to the UI inside every `snippets` snapshot and the `tagNames` initialisation blob; `App.jsx` keeps it in `tagNames` state and `SnippetList`/`SnippetCard` fall back to the built-in labels when a key is absent
- `setLooperRecording`, `setLooperPlaying`, `setLooperLooping`, `setLooperOverdub` (`{ enabled }` — with a loop captured and looping on, record layers over the loop instead of replacing it; the loop starts from position 0 when the layer capture begins, so it stays silent through a count-in and the layer aligns with the loop downbeat; the frontend disables the switch in one-shot mode), `setLooperCountIn`, `setLoopCrop`, `clearLoop`, `saveLoop`
- The looper is **audio-only** (the C++ MIDI-event machinery was removed — see the juce-audio skill). Transport snapshots expose `looperRecording`, `looperPreRoll`, `looperPlaying`, `looperLooping`, `looperOverdub`, `looperCountInBeats`, `looperCropStartBeats`, `looperCropEndBeats`, `audioLoopStart` / `audioLoopPosition` / `audioLoopLength`, `audioLoopPeaks` (waveform buckets for the timeline, re-rendered via the shared `Waveform` component), and `maxRecordSamples` (record-buffer capacity — the frontend derives fresh-capture progress from `audioLoopLength / maxRecordSamples`). Use `audioLoopPosition` / `audioLoopLength` for the visual timeline and playhead. The playhead is **rAF-interpolated** (a `useLayoutEffect` in `Looper.jsx` extrapolates from each 30 Hz push at `recordingSampleRate`, with a 20 ms deadband so push latency can't nudge it backwards, and writes `style.left` straight to the DOM) — it no longer uses a CSS `left` transition, so it tracks the audio without the old ~80 ms lag and **snaps forward at the loop seam** (a brief `.is-seam` opacity dip marks the wrap; one-shot clamps at the end). The timeline also draws a beat/bar ruler behind the waveform (`.looper-ruler`, two repeating-gradient layers whose beat/bar spacing comes from inline `--beat-width`/`--bar-width` CSS vars) with a numbered bar label every 1/2/4 bars (`.looper-ruler-labels`, thinned by `barLabelStep`; per-beat ticks dropped past 32 beats), all derived from the crop-aware `totalBeats`/`totalBars`. While recording, `Looper.jsx` shows an input VU meter (`inputLevel`/`inputPeak`, reserved slot so controls don't shift) and the state pill reads "Overdub" during a layered capture.
- `setLoopCrop` payloads are `{ startBeats, endBeats }` in whole beats (4 per bar); `setLooperCountIn` is `{ beats: 0..8 }`; `setLooperRecording` / `setLooperPlaying` / `setLooperLooping` are `{ enabled }`. The looper has **no per-section click or MIDI-clock toggles** — the header owns those (`setMetronome`, `setClickDuringCapture`, `setMidiClock`).
- `saveLoop` converts the captured (cropped) audio loop into a library snippet and opens the same WAV + JSON save dialog as the take recorder (`saveSnippetWithDialog`). There is no MIDI `.mid` save/load.
- `addVst3`, `removeVst3`, `moveVst3`, `setVst3Bypass`, `setVst3MidiPass`, `openVst3Editor`, `closeVst3Editor`
- `scanVst3Folder`, `getVst3Chain`, `blockVst3Plugin`, `unblockVst3Plugin`
- `addChain`, `removeChain`, `renameChain`, `setChainInputs`, `setChainRecord`, `setChainVolume`, `setChainMute`, `setChainMidiChannels`
- `resizeEditor` (`{ dWidth }` — device-pixel width delta from the corner resize grip; the C++ editor applies the locked aspect ratio + limits and resizes its window). See `ResizeHandle.jsx` and the "Fixed-design scaling shell" note below.
- `setChainMonitorSolo` (`{ chain, solo }`) / `setChainMonitorMute` (`{ chain, muted }`) — monitor-only: solo isolates soloed chains in the monitor (dry muted, capture unchanged), monitor-mute drops one chain from the monitor (capture unchanged). `PluginChain.jsx` shows them as the cobalt **Solo** and amber **Mon** toggles beside the hard **Mute**; the subtitle appends "solo"/"mon-muted".
- Chain snapshots (`backendVst3Chain`) ship a `chains` array: each entry is `{ id, name, inputs: [0..7], wantsMidi, midiChannels: [1..16], recordOnCapture, volume (dB), muted, monitorSolo, monitorMuted, pending, slots }` plus a top-level `inputChannels` count. `setVst3MidiPass` / `setChainInputs` / `setChainRecord` / `renameChain` / `setChainVolume` / `setChainMute` / `setChainMidiChannels` / `setChainMonitorSolo` / `setChainMonitorMute` payloads are `{ chain: <chain id>, ... }` (`setChainInputs` carries `inputs: [0..7]`, `setChainMidiChannels` carries `channels: [1..16]`, `setChainVolume` carries `volume` in dB). There is no `CHAIN_IDS` constant — chain ids are generated by the backend ("chain0", "chain1", …).
- Live per-chain meters arrive on the 30 Hz `backendTransport` push as `chainLevels: [{ chain, level, peak }]`.

**Backend events** (C++ → React):
- `parameters` — `{ input: number, output: number }` (both dB; Input is the record trim, Output the master monitor gain)
- `snippets` — Array of snippet objects OR `{ snippets: [...], libraryFolder, lastSaveError, tagNames }` (`tagNames` is `{ colorKey: userName }`, sent with every library snapshot)
- `transport` — Full transport state object, including `chainLevels`, plus `loopLevel`, `dryLevel`, `clickPitch`/`clickAccentPitch`/`clickDecay`/`clickVolume`/`clickAccentVolume`/`clickNoise`, `metronomeEnabled`, `clickDuringCapture`, `midiClockEnabled`/`midiClockOnRecord`, `takePending`/`takeLength`/`takePlaying`/`takePosition`/`takePeaks`, `midiOutputDevice`, and `midiOutputDeviceList`
- The **sync strip** (`SyncControls.jsx`, beside the recording tabs) owns the header-level MIDI clock + click UI: the **Clock** toggle (`setMidiClock`), the **Clock: on record** toggle (`setMidiClockOnRecord`, default off), the **Click** toggle (`setMetronome`), the **Click: capture** toggle (`setClickDuringCapture`), the **Click sound** popover (`setClickParams`) and the MIDI out device picker (`setMidiDevice`). The clock's run-condition behavior (free-run vs captures-only, direct Start/Stop to the device) is documented in AGENTS.md "MIDI clock" — this component is the UI surface for it. There is no standalone MidiClock section anymore.
- `notify` — `{ message: string, level: "info"|"warn"|"error" }`
- `vst3Chain` — `{ chains: [{id, name, inputs, wantsMidi, midiChannels, recordOnCapture, volume, muted, pending, slots}], openEditors, plugins, folder, blocklist, inputChannels, restoring, restoreError }` — `pending` is the number of saved slots still loading for that chain; `restoring` is true while the deferred restore is in flight (see the splash pattern below); `restoreError` carries the last restore failure text
- `vst3ScanProgress` — `{ active, current, total, currentFile, folder }`
- `vst3LoadFailed` — Per-plugin load failure notification

**Chain selection**: VST3 slot events carry a `chain` field with the target chain's id (e.g. `"chain0"`); the backend falls back to the first chain when the field is absent. Legacy `"midiChain"`/`"audioChain"` ids exist only in old saved states, which `applyChainState` migrates on load (see AGENTS.md "Persistence").

## Component Patterns

- **App.jsx** is the root. It subscribes to all backend events in `useEffect` hooks (with cleanup) and passes state down as props.
- **Emit pattern**: `setState()` immediately (optimistic UI), then `emit()` to C++.
- **Initial data**: Read from `getInitialData()` via `useMemo` for default state values.
- **Explicit snapshot requests**: Fire `FRONTEND_EVENTS.getSnippets` and `FRONTEND_EVENTS.getVst3Chain` **once** on mount to compensate for lost listener notifications during startup — emitting from inside that event's own `subscribe` handler on every tick would loop forever. `App.jsx` also emits `getVst3Chain` on mount (coalesces with `PluginChain`'s emit) so the splash learns the restore state early.
- **Startup splash** (`SplashScreen.jsx`): full-screen overlay driven by the first `vst3Chain` snapshot (the backend's `restoring` flag + per-chain `pending` counts; the restore story itself is in AGENTS.md "Deferred chain restore"). Frontend mechanics: hold a `SPLASH_MIN_MS` minimum display time, re-appear if a later host state load starts another restore, keep splash state in `App.jsx` (`splashPhase` + `mountedAt` ref + two `useEffect`s, one for the min-delay fade, one for the fade-out timer). Don't block the app on it — it's an overlay, and the app renders underneath.
- **Recording tabs** (`recordingMode` in `App.jsx`): the one-shot take recorder (Transport + TakeReview) and the looper sit behind a Take/Loop tab bar so only one recording approach is on screen at a time. Tab state is `"take" | "loop"`, persisted in `localStorage` under `bp:recordingMode`, default `"take"`. The Take tab shows a small red `.recording-tab-badge` while `transport.takePending` (unsaved take) — no auto-switching. Tab switching is frontend-only: recording/loop playback state lives in the backend and keeps running across tab changes.
- **BPM lives in the header** (`HeaderControls.jsx`, first knob in `.header-knobs`), shared by the take recorder, the looper (beat math / crop) and the MIDI clock. The take Transport keeps only its count-in field. `Knob` in `controls.jsx` accepts a `title` prop (tooltip on the shell).
- **Takes toolbar** (`SnippetList.jsx`): all filtering/sorting is frontend-only — a search input (matches name/comments/notes/key), a sort select (newest/oldest/name/longest/shortest), a key filter select (distinct detected keys + "no key"), and a wrap row of tag-colour filter chips (multi-select, incl. an untagged chip) with a Clear button when any filter is active. The "Tags" button opens a rename popover with one input per colour (`renameTag`). The section count shows `filtered / total` while filtering.
- **Takes grid cap**: `SnippetList` renders the first `VISIBLE_PAGE` (10) takes of the filtered/ordered list with a "Show more" button (+`SHOW_MORE_STEP` 10, shows `visible / total`); `visibleCount` resets to the cap whenever query/sort/key/tag filters change.
- **Snippet card summary**: two-row layout. `.snippet-title-line` (tag dot + `.snippet-name`, `flex: 1 1 100%`) is the full-width first row so a long name never competes with the chips; the key/notes chips, duration (`margin-left: auto`) and date wrap onto a second row.

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
- Component-level styles use class names prefixed with the component name (e.g., `.transport-`, `.snippet-`, `.fx-chain-`, `.sync-`, `.bp-splash-` for the splash overlay).
- Layout uses flexbox. The app is a single-page vertical layout with `header`, the recording tab bar, the active recording panel (Transport + TakeReview on the Take tab, Looper on the Loop tab), `PluginChain`, `library-section`, and `footer`; the splash is a `position: fixed` overlay with a high `z-index`. The header's right side (`.header-knobs`) holds only the monitoring knobs — BPM, Input, Output (the looper's Loop knob lives in `Looper.jsx`). The click + MIDI clock toggles live in the **sync strip** (`.sync-strip`), a contained pill bar in the `.record-tools` row next to `.recording-tabs`: two `.sync-group` clusters (click: `.sync-toggle` pills + `.sync-text` "Click sound" button + `.click-popover`; MIDI clock: the Clock / On record toggles + `.sync-device` MIDI out select) separated by a `.sync-divider`. Everything flex-wraps on narrow widths.
- **Fixed-design scaling shell**: `main.jsx` wraps `<App/>` in `UiScale.jsx`, which lays the UI out once at a fixed 960px design width (`.ui-content`) and scales that surface to fill the aspect-locked editor window (Neural DSP style) — it uses **`transform: scale()`** (`translateX(offsetX) scale(s)`, origin top-left), NOT the CSS `zoom` property. `s = min(scrollClientWidth/960, scrollClientHeight/700)`, measured from `.ui-scroll`, **never** `window.innerWidth` (it includes the vertical scrollbar). `transform` is deliberate over `zoom`: Chromium's `zoom` is under-specified and **mis-paints dynamically-updated content** (pressing a chain **Solo** left the lower part of the window blank until a forced repaint). Transforms don't affect layout, so `.ui-scaled` reserves the scaled scroll height and a `ResizeObserver` keeps it in sync as content grows/shrinks; and a transformed element traps `position: fixed` descendants, so the splash and notification toast are portaled (below). **`.ui-content` is a flex column, so `.app` is a flex item: it must keep `width: 100%; min-width: 0`** — the default `min-width: auto` lets the app grow to its content's (font-metric-dependent) min-content width, which pushed the real Windows WebView2 build ~150px past the viewport and cut off the right side. Headless Chrome's fonts didn't reproduce it, so test UI layout changes in a real WebView2. `.ui-scroll` owns the vertical scrollbar and uses `scrollbar-gutter: stable`; `.ui-content` sets inline `min-height = clientHeight / scale`. Because the layout width is fixed, viewport-width `@media` reflow breakpoints are gone — don't add new ones; size things for the 960 base and they scale. **WebView2 page-zoom is suppressed in `main.jsx`** (non-passive `wheel`/`keydown` listeners `preventDefault` on Ctrl/Cmd — JUCE exposes no zoom-control option under `withWinWebView2Options`): page zoom changes the CSS pixel, so the self-scaling UI stays put while the fixed-size corner grip changes size, which reads as "Ctrl-scroll only resizes the grip". **The splash and the notification toast are portaled to `document.body`** (`createPortal` in `App.jsx`) so they render outside the transformed `.ui-content` at a constant default size (the splash no longer scales with the window); the during-splash scroll lock is therefore `html:has(.bp-splash) .ui-scroll { overflow: hidden }`.
- **Corner resize grip**: the plugin window has **no** OS/host resize border (`WebViewEditor` calls `setResizable(false, false)`) because WebView2 is a native child HWND that occludes JUCE components, so JUCE's built-in corner would be invisible. Instead `UiScale` renders `ResizeHandle.jsx` (`.bp-resize-handle`, a 44px bottom-right hit area with a diagonal hatch, `nwse-resize`), which emits `resizeEditor` with a device-pixel `dWidth` on pointer drag. Use `screenX` (not `clientX`) for the delta — client coords feed the resize back into itself as the window grows under the pointer — and `setPointerCapture` so the drag survives leaving the element.
- **Recording tabs**: `.recording-tabs` is a segmented pill bar (`display: inline-flex`, inset background, pill radius) containing `.recording-tab` buttons; the active tab gets `is-active` (white pill + hairline border + shadow). The `.recording-tab-badge` is a 7px red dot marking a pending take. Uses `role="tablist"` / `role="tab"` / `role="tabpanel"` with `aria-selected` / `aria-controls` / `aria-labelledby`.
- **Chain rail**: `.fx-chain-panels` is a **horizontal** flex rail (`overflow-x: auto`, `scroll-snap-type: x proximity`), not a grid — each `.fx-chain-panel` is `flex: 0 0 340px` with `scroll-snap-align: start`, so adding chains scrolls sideways and never stretches the page vertically. `.fx-chain-slots` scrolls internally (`flex: 1; min-height: 120px; max-height: 240px; overflow-y: auto`) so a long plugin list stays inside its card; `align-items: stretch` keeps cards equal height. There is no `@media (max-width: 900px)` single-column fallback anymore.
- Controls that reveal extra UI on demand: absolutely position the revealed element below its anchor so sibling controls never shift (e.g. `.click-popover` under the sync strip, `.snippet-tags-popover` under the Tags button). The MIDI clock's device selector is inline in the sync strip (`.sync-device`, always visible, not revealed on demand).
- **Looper layout**: `.looper-controls` stacks the transport row (`.looper-primary-controls`, record + reserved input-meter slot + play/discard/save) over `.looper-settings` (top rule + padding). Settings are clustered into `.looper-setting` groups — levels (Loop knob + loop meter, Dub knob), count-in, mode (Loop/Overdub switches stacked), crop — top-aligned with `align-items: stretch` so each group's trailing hairline (`:not(:last-child)` `border-right`) spans the row; the strip wraps on narrow widths with the new line starting cleanly. Keep the Loop/Dub knobs top-aligned with each other (don't bottom- or center-align the levels group).
- **Snippet summary**: `.snippet-summary` is `flex-wrap: wrap`; `.snippet-title-line` (tag dot + name) is `flex: 1 1 100%` on its own row, and the chips/duration/date flow to a second row — never put the name and the chips in one shrinkable flex row or the name collapses (that was the pre-fix clipping bug).
- `ErrorBoundary.jsx` wraps the snippet library rows in `App.jsx`; a crashing child must never blank the whole WebView (white screen). Keep new volatile components behind it.
- Respect `prefers-reduced-motion`: the splash's logo pulse and indeterminate bar sweep are killed in the existing reduced-motion block — add new cosmetic animations there too.

## Don't

- Keep UI-only page state (recording tab, list caps, filters) in the backend — hold it in React state, persisted to `localStorage` where it should survive restarts.
- Use class components — this codebase is functional components + hooks only.
- There are no ESLint/Prettier configs; match the surrounding style by hand (2-space indentation in JSX, `const` arrow functions for components).
