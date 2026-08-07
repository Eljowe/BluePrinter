# BluePrinter (JUCE + CMake + WebView2)

A minimal JUCE audio plugin template with a WebView2 (Edge) editor and a React +
Vite frontend. Designed to be forked and customised.

This fork of the template turns the plugin into a **guitar take recorder**:
hit record, play, stop, name the take, write down what to work on, then save
to disk (WAV + sidecar JSON).

## Install (no source code needed)

Windows 10/11 only. Grab the latest installer from the
[Releases page](../../releases):

```
BluePrinterSetup-1.0.0.exe
SHA256SUMS.txt
```

Run the installer — it needs admin rights once (SmartScreen may warn
"Windows protected your PC" because the installer is not code-signed; use
"More info → Run anyway" for now). It installs:

- **BluePrinter.vst3** → `C:\Program Files\Common Files\VST3\` (every DAW
  scans this folder, so it appears after a plugin rescan — no registry
  fiddling)
- **BluePrinter.exe** (standalone app) → `C:\Program Files\BluePrinter\`,
  with a Start Menu shortcut

The installer also checks for the **Microsoft VC++ redistributable** and
the **WebView2 Runtime** and downloads + installs them silently only when
they are missing (WebView2 ships with Windows 11, so it is usually skipped).

Uninstalling via Settings → Apps → Installed apps → BluePrinter removes the
program files and shortcuts but **keeps your settings and snippet library**.

Verify the download before running (optional):

```powershell
Get-FileHash .\BluePrinterSetup-1.0.0.exe -Algorithm SHA256
# compare against SHA256SUMS.txt
```

If you build from source instead, the release bundle can be regenerated with
the **Build Release Bundle** VS Code task (`installer/build-release.ps1`).

## What it does

- **Live recording** of the audio flowing through the plugin into an in-memory
  library. Up to 120 seconds per take at the current sample rate (stereo,
  32-bit float). The pre-allocated record buffer means the audio thread
  never allocates.
- **Naming and notes** — every take has a name (up to 80 chars) and a comments
  field (up to 2000 chars). Edits are committed on blur.
- **Playback** through the plugin's output bus. Overrides monitoring while
  playing.
- **Save to disk** — after stopping, the take stays in memory as a pending
  take you can replay; then save it to the library (16-bit WAV plus a JSON
  sidecar: name, comments, sample rate, channel count, duration, creation
  time) or discard it. If a library folder is set, "Save to library" writes
  the WAV + sidecar there.
- **Reveal in Explorer** — opens the saved WAV in Windows Explorer.
- **Live level meter** on the transport, with peak hold.
- **Snippet list** with per-take waveform thumbnail (downsampled peaks).
- **Musical key detection** — FFT-based (Krumhansl-Schmuckler) analysis that
  tags a snippet with its key and pitch classes.
- **Metronome, BPM and count-in** — percussive click with a configurable
  count-in before recording starts.
- **MIDI clock output** — 24 ppqn clock, Start/Stop to a selectable MIDI
  output device for syncing drum machines and sequencers.
- **Flexible VST3 FX chains** — any number of parallel chains
  (arpeggiators, chord generators, instruments, amp sims, EQ, reverb),
  each selecting which input channels feed it (from a 1–8 channel input
  bus — checkboxes per input, or none for MIDI-only chains), a
  per-chain **MIDI toggle** plus a **MIDI channel filter** (1–16) so
  note-aware plugins only see the keys you want, and a **Record toggle**
  so you choose which chains get baked into takes and loops. Every chain
  has its own **volume knob**, **mute** switch, and live **level meter**.
  A synth chain can sit next to a guitar chain without either clobbering
  the other's signal — chains never hear each other.
- **Audio looper** — capture a loop of the record mix (dry input +
  selected chains — guitar, synth sounds, FX — all baked in), with the
  same click + count-in + MIDI-clock controls as the take recorder,
  beat-stepped start/end cropping, and loop/one-shot playback. Saving a
  loop converts it into a library snippet using the same one-click flow
  as the take recorder. Audio-only: there is no MIDI event sequencing or
  `.mid` export.

The existing `Gain` parameter is kept and wired through the APVTS so you can
trim monitoring level while recording.

## What's in the box

- **Snippet library** (`Source/SnippetLibrary.{h,cpp}`) — mutex-protected
  vector of `shared_ptr<Snippet>`. Audio data is held as
  `shared_ptr<const AudioBuffer<float>>` so the audio thread's playback
  pointer can't dangle when a snippet is deleted.
- **Audio processor** (`Source/PluginProcessor.{h,cpp}`) — pass-through with
  a `Gain` parameter plus the transport state machine (`Recording`,
  `Playing`, level meter, recording buffer), the audio looper,
  metronome, and MIDI clock.
- **Native fallback editor** (`Source/PluginEditor.{h,cpp}`) — used when
  WebView2 is not available.
- **WebView2 editor** (`Source/WebViewEditor.{h,cpp}`) — main editor.
  Serves the built React app from `WebUI/dist/` and bridges recording,
  playback, snippet metadata, looper, and file-dialog events.
- **VST3 chains** (`Source/PluginChain.{h,cpp}`) — a flexible list of
  parallel chains (ids, names, input masks, MIDI + record toggles,
  volume/mute/level meters) with async plugin loading, per-slot bypass,
  native editor windows, and state persistence.
- **VST3 scanner** (`Source/Vst3Library.{h,cpp}`) — folder scanning with a
  blocklist and async per-file description.
- **React + Vite frontend** (`WebUI/`) — transport bar, library folder row,
  snippet list with editable name/comments, waveform, level meter, audio
  looper, plugin-chain panels, toast notifications.

## Prerequisites

- Windows 10/11
- Visual Studio 2022 with the *Desktop development with C++* workload
- CMake 3.22+
- Node.js 18+ and npm
- JUCE source tree at `C:/JUCE/JUCE` (override with `-DJUCE_DIR=...` if yours
  is elsewhere)
- Microsoft Edge **WebView2 Runtime** (preinstalled on Windows 10/11 in
  most setups; otherwise the editor falls back to a plain message)

## Configure

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

If JUCE is elsewhere:

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DJUCE_DIR="C:/path/to/JUCE"
```

## Build

```
cmake --build build --config Debug
```

Build only the standalone app target:

```
cmake --build build --config Debug --target BluePrinter_Standalone
```

Build only the VST3 target:

```
cmake --build build --config Debug --target BluePrinter_VST3
```

## Run

```
.\build\BluePrinter_artefacts\Debug\Standalone\BluePrinter.exe
```

Or in one PowerShell command:

```
cmake --build build --config Debug --target BluePrinter_Standalone; Start-Process -FilePath ".\build\BluePrinter_artefacts\Debug\Standalone\BluePrinter.exe"
```

## WebView UI (React + Vite)

The editor loads the built frontend from `WebUI/dist/index.html`. The Vite
output is bundled into the plugin and served by the WebView2 component.

### 1) Install dependencies

```
cd WebUI
npm install
```

### 2) Build the frontend

```
cd WebUI
npm run build
```

### 3) Hot-reload dev server (optional)

```
cd WebUI
npm run dev
```

In the same shell session, point the editor at it before launching the
plugin:

```
$env:BLUEPRINTER_WEB_UI_URL="http://127.0.0.1:5173"
cmake --build build --config Debug --target BluePrinter_Standalone; Start-Process -FilePath ".\build\BluePrinter_artefacts\Debug\Standalone\BluePrinter.exe"
```

## How the bridge works

Events flow through `window.__JUCE__.backend`:

| Frontend → Backend                                        | Purpose                                            |
| --------------------------------------------------------- | -------------------------------------------------- |
| `frontendSetParameter`                                    | Update an APVTS parameter (`Gain`, `PlaybackVolume`) |
| `frontendStartRecording` / `frontendStopRecording`        | Transport: record toggle                           |
| `frontendSetTakePlayback` / `frontendSaveTake` / `frontendDiscardTake` | Pending-take review: play the take / save it to the library / discard it |
| `frontendStartPlayback` / `frontendStopPlayback`          | Transport: play a snippet id / stop                |
| `frontendUpdateSnippetMeta` / `frontendDeleteSnippet`     | Edit name + comments / remove a snippet            |
| `frontendDetectSnippetKey`                                | Run key detection on a snippet                     |
| `frontendSaveSnippet`                                     | Open a save dialog and write WAV + JSON            |
| `frontendSaveLoop`                                        | Save the captured looper loop as a library snippet |
| `frontendRevealSnippet`                                   | Reveal the saved file in Explorer                  |
| `frontendChooseLibraryFolder` / `frontendOpenLibraryFolder` | Pick / open the library folder                   |
| `frontendRefreshLibrary` / `frontendGetSnippets`          | Re-scan the folder / request a fresh snapshot      |
| `frontendSetMetronome` / `frontendSetBpm` / `frontendSetCountInBeats` | Metronome + count-in settings        |
| `frontendSetMidiClock` / `frontendSetMidiDevice`          | MIDI clock output on/off + output device           |
| `frontendSetLooperRecording` / `...Playing` / `...Looping` | Looper: record / play / loop toggle              |
| `frontendSetLooperClick` / `frontendSetLooperCountIn` / `frontendSetLooperClickDuringCapture` | Looper: click + count-in beats + click-through-capture gate |
| `frontendSetLoopCrop` / `frontendClearLoop` / `frontendSaveLoop` | Looper: crop start/end **beats** / clear / save as snippet to the library folder |
| `frontendAddVst3` / `frontendRemoveVst3` / `frontendMoveVst3` | VST3 chain: add / remove / reorder slots       |
| `frontendSetVst3Bypass` / `frontendOpenVst3Editor` / `frontendCloseVst3Editor` | Chain slot bypass + native editor |
| `frontendSetVst3MidiPass`                               | Per-chain MIDI pass-through toggle (default: FX chain off) |
| `frontendScanVst3Folder` / `frontendGetVst3Chain`         | VST3 scan / chain snapshot                        |
| `frontendBlockVst3Plugin` / `frontendUnblockVst3Plugin`   | Blocklist management                              |

| Backend → Frontend       | Purpose                                                  |
| ------------------------ | -------------------------------------------------------- |
| `backendParameters`      | Current APVTS parameter snapshot                         |
| `backendTransport`       | Recording state, level meter, playback position, looper state |
| `backendSnippets`        | Snippet list (id, name, comments, sample-rate, peaks)   |
| `backendNotify`          | Toast notification (info / ok / error)                   |
| `backendVst3Chain`       | Chain slots + available plugin list                      |
| `backendVst3ScanProgress`| Scan progress (file-by-file)                             |
| `backendVst3LoadFailed`  | Per-plugin load failure notification                     |

The React app subscribes via `window.__JUCE__.backend.addEventListener` and
emits via `window.__JUCE__.backend.emitEvent`. Initial state is provided
through `withInitialisationData("parameters" | "snippets" | "transport", ...)`.

## Recording model

- The audio thread **never allocates**. A 120 s stereo float buffer is
  pre-allocated in `prepareToPlay`. Recording writes into it with a lock
  (the message thread acquires the same lock only to copy the final take
  into a new buffer).
- After the user clicks stop, the message thread finalises: the take is **not
  saved automatically** — it becomes a pending take (waveform peaks computed,
  length stored) that the UI offers for replay, then an explicit
  **Save to library** (copies the audio into a new snippet and writes WAV +
  sidecar when a library folder is set) or **Discard**. Any new take or loop
  capture invalidates the pending take.
- Playback stores the snippet pointer as a `shared_ptr` on the audio
  thread, so deleting a snippet from the library can't dangle an
  in-flight playback.

## Audio looper

The looper panel is audio-only. It captures whatever the plugin chains
produce, so the loop sounds exactly like what you heard while recording:

- Capture taps the **record mix** (dry post-gain input + the chains whose
  Record toggle is on, before the metronome click is mixed) into the shared
  pre-allocated `recordBuffer`, so synth sounds and FX from the selected
  chains are baked in and deselected chains are left out.
- Its own **click + count-in** run off the same metronome clock; the click is
  mixed after the capture tap so it never ends up in the loop. The looper
  shares the header-level click and MIDI clock controls with the take
  recorder (no per-section toggles): the master **Click** on/off
  (`metronomeEnabled`), **Click: capture** (`clickDuringCapture`, click
  silent through the capture when off, count-in only), a per-mode count-in
  field, and the **Clock** / **Clock: on record** MIDI clock toggles — plus
  the global **Click sound** tuning (pitch/snap/volume) in the sync strip
  beside the recording tabs, which the take and the looper share.
- On stop, the captured length is trimmed to the nearest full 4/4 bar
  (beat-length fallback). **Crop start / end** steppers trim in **whole
  beats** (4 per bar at the current BPM) off either side — the audible window
  is `[audioLoopStart, audioLoopStart + audioLoopLength)`. The timeline shows
  a live waveform of the cropped loop, with the trimmed regions shaded.
- Playback mixes the loop over the live input, post-chain (the loop audio is
  already processed, so it isn't re-run through the chains), with a
  precomputed crossfade at the wrap point. Loop/one-shot is toggleable.
- **Save to library** converts the cropped loop into a library snippet
  (`BluePrinterAudioProcessor::saveLoopSnippet()`, message thread only) and —
  exactly like the take recorder — writes WAV + JSON to the library folder
  when one is set (no dialog). The library snippet is the only persistence
  story — there is no MIDI `.mid` export, and MIDI event
  recording/quantization was removed from the looper entirely
  (MIDI-listening chains still play instruments live).
- The looper and the take recorder share `recordBuffer` and preempt each
  other, so they never capture simultaneously.

## Renaming the plugin

Update these together:

- `CMakeLists.txt` — `project(...)`, `juce_add_plugin(... PRODUCT_NAME ...)`,
  `BUNDLE_ID`, `PLUGIN_MANUFACTURER_CODE`, `PLUGIN_CODE`, and the
  `WebView2Cache` folder name in `Source/WebViewEditor.cpp`.
- `Source/PluginProcessor.{h,cpp}` — class name and `createFilter()`.
- `Source/PluginEditor.{h,cpp}` — class name.
- `Source/WebViewEditor.{h,cpp}` — class name.
- `WebUI/index.html` — `<title>`.
- `WebUI/package.json` — `name`.
- `WebUI/src/App.jsx` — branding.
- `.vscode/tasks.json` — task labels and the `BLUEPRINTER_WEB_UI_URL` env var.

## Testing

This repository has no CTest tests configured. Run the standalone, hit
**RECORD**, play something into the input (the standalone hosts a virtual
input that you can route from your DAW or any source), stop, then verify the
take-review panel lets you replay the take and either **Save** it (it appears
in the snippet list with editable name and comments, and the WAV + JSON are
written to the library folder when one is set) or **Discard** it.

## VS Code tasks

Provided under Terminal -> Run Task:

- **Install WebUI dependencies**
- **Build WebUI**
- **Build BluePrinter Debug**
- **Rebuild BluePrinter Debug**
- **Run BluePrinter Debug**
- **Build + Install + Run BluePrinter (Release, per-user)** — builds, installs
  to `%USERPROFILE%\BluePrinter` and launches
- **Install VST3 to Program Files (elevated)** — UAC copy of the built VST3
  into the standard DAW scan folder
- **Build Installer (Inno Setup)** — compiles `installer/BluePrinter.iss`
  into `build/installer/BluePrinterSetup-<version>.exe`
- **Build Release Bundle** — WebUI + Release build, then assembles
  `build/release/BluePrinter-<version>/` with the installer, LICENSE,
  README.md and SHA256SUMS.txt
- **Create GitHub Release (draft)** — tags the default branch and creates a
  draft release with all bundle files as assets (needs `$env:GH_TOKEN` with
  `repo` scope; run with `-Published` to skip the draft)
