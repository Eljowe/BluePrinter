# BluePrinter (JUCE + CMake + WebView2)

A minimal JUCE audio plugin template with a WebView2 (Edge) editor and a React +
Vite frontend. Designed to be forked and customised.

This fork of the template turns the plugin into a **guitar take recorder**:
hit record, play, stop, name the take, write down what to work on, then save
to disk (WAV + sidecar JSON).

## What it does

- **Live recording** of the audio flowing through the plugin into an in-memory
  library. Up to 120 seconds per take at the current sample rate (stereo,
  32-bit float). The pre-allocated record buffer means the audio thread
  never allocates.
- **Naming and notes** — every take has a name (up to 80 chars) and a comments
  field (up to 2000 chars). Edits are committed on blur.
- **Playback** through the plugin's output bus. Overrides monitoring while
  playing.
- **Save to disk** — choose a library folder, then per-take "Save" writes a
  16-bit WAV plus a JSON sidecar (name, comments, sample rate, channel count,
  duration, creation time). If the library folder is set, every new take is
  auto-saved there.
- **Reveal in Explorer** — opens the saved WAV in Windows Explorer.
- **Live level meter** on the transport, with peak hold.
- **Snippet list** with per-take waveform thumbnail (downsampled peaks).
- **Musical key detection** — FFT-based (Krumhansl-Schmuckler) analysis that
  tags a snippet with its key and pitch classes.
- **Metronome, BPM and count-in** — percussive click with a configurable
  count-in before recording starts.
- **MIDI clock output** — 24 ppqn clock, Start/Stop to a selectable MIDI
  output device for syncing drum machines and sequencers.
- **Two VST3 FX chains** — a `midiChain` (arpeggiators, chord generators,
  instruments) that runs before an `audioChain` (amp sims, EQ, reverb), so a
  synth in the MIDI chain can't clobber the guitar signal.
- **Audio looper** — capture a loop of whatever the plugin chains produce
  (guitar, synth sounds from the MIDI chain, FX — all baked in), with its
  own click + count-in, bar-stepped start/end cropping, and loop/one-shot
  playback. Saving a loop converts it into a library snippet using the
  same WAV + JSON flow as the take recorder. Audio-only: there is no MIDI
  event sequencing or `.mid` export.

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
- **VST3 chains** (`Source/PluginChain.{h,cpp}`) — two parallel chains
  (`midiChain`, `audioChain`) with async plugin loading, per-slot bypass,
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
| `frontendSetLooperClick` / `frontendSetLooperCountIn`     | Looper: click + count-in beats                     |
| `frontendSetLoopCrop` / `frontendClearLoop` / `frontendSaveLoop` | Looper: crop start/end bars / clear / save as snippet |
| `frontendAddVst3` / `frontendRemoveVst3` / `frontendMoveVst3` | VST3 chain: add / remove / reorder slots       |
| `frontendSetVst3Bypass` / `frontendOpenVst3Editor` / `frontendCloseVst3Editor` | Chain slot bypass + native editor |
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
- After the user clicks stop, the message thread finalises: it allocates a
  buffer sized to the actual take, copies the data, computes 256-point
  peak data for the waveform thumbnail, and adds the snippet to the
  library.
- Playback stores the snippet pointer as a `shared_ptr` on the audio
  thread, so deleting a snippet from the library can't dangle an
  in-flight playback.

## Audio looper

The looper panel is audio-only. It captures whatever the plugin chains
produce, so the loop sounds exactly like what you heard while recording:

- Capture taps the **post-chain** signal (after both VST3 chains, before the
  metronome click is mixed) into the shared pre-allocated `recordBuffer`, so
  synth sounds from the MIDI chain and FX from the audio chain are baked in.
- Its own **click + count-in** run off the same metronome clock; the click is
  mixed after the capture tap so it never ends up in the loop.
- On stop, the captured length is trimmed to the nearest full 4/4 bar
  (beat-length fallback). **Crop start / end** steppers trim whole bars off
  either side — the audible window is `[audioLoopStart, audioLoopStart +
  audioLoopLength)`.
- Playback mixes the loop over the live input, post-chain (the loop audio is
  already processed, so it isn't re-run through the chains), with a
  precomputed crossfade at the wrap point. Loop/one-shot is toggleable.
- **Save loop** converts the cropped loop into a library snippet
  (`BluePrinterAudioProcessor::addLoopSnippet()`, message thread only) and
  opens the same WAV + JSON save dialog as the take recorder. The library
  snippet is the only persistence story — there is no MIDI `.mid` export,
  and MIDI event recording/quantization was removed from the looper
  entirely (the MIDI chain still plays instruments live).
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
input that you can route from your DAW or any source), and verify the take
appears in the snippet list with editable name and comments. Then click
**Save** and confirm the WAV + JSON are written to your chosen library
folder.

## VS Code tasks

Provided under Terminal -> Run Task:

- **Install WebUI dependencies**
- **Build WebUI**
- **Build BluePrinter Debug**
- **Rebuild BluePrinter Debug**
- **Run BluePrinter Debug**
