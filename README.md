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

The existing `Gain` parameter is kept and wired through the APVTS so you can
trim monitoring level while recording.

## What's in the box

- **Snippet library** (`Source/SnippetLibrary.{h,cpp}`) — mutex-protected
  vector of `shared_ptr<Snippet>`. Audio data is held as
  `shared_ptr<const AudioBuffer<float>>` so the audio thread's playback
  pointer can't dangle when a snippet is deleted.
- **Audio processor** (`Source/PluginProcessor.{h,cpp}`) — pass-through with
  a `Gain` parameter plus the transport state machine (`Recording`,
  `Playing`, level meter, recording buffer).
- **Native fallback editor** (`Source/PluginEditor.{h,cpp}`) — used when
  WebView2 is not available.
- **WebView2 editor** (`Source/WebViewEditor.{h,cpp}`) — main editor.
  Serves the built React app from `WebUI/dist/` and bridges recording,
  playback, snippet metadata, and file-dialog events.
- **React + Vite frontend** (`WebUI/`) — transport bar, library folder row,
  snippet list with editable name/comments, waveform, level meter, toast
  notifications.

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

| Frontend → Backend                                  | Purpose                                            |
| --------------------------------------------------- | -------------------------------------------------- |
| `frontendSetParameter`                              | Update an APVTS parameter                          |
| `frontendStartRecording` / `frontendStopRecording`  | Transport: record toggle                           |
| `frontendStartPlayback` / `frontendStopPlayback`    | Transport: play a snippet id / stop                |
| `frontendUpdateSnippetMeta`                         | Edit name + comments of a snippet                  |
| `frontendDeleteSnippet`                             | Remove a snippet from the library                  |
| `frontendSaveSnippet`                               | Open a save dialog and write WAV + JSON            |
| `frontendRevealSnippet`                             | Reveal the saved file in Explorer                  |
| `frontendChooseLibraryFolder`                       | Open a folder picker for the library folder        |
| `frontendOpenLibraryFolder`                         | Open the library folder in Explorer                |

| Backend → Frontend       | Purpose                                                  |
| ------------------------ | -------------------------------------------------------- |
| `backendParameters`      | Current APVTS parameter snapshot                         |
| `backendTransport`       | Recording state, level meter, playback position          |
| `backendSnippets`        | Snippet list (id, name, comments, sample-rate, peaks)   |
| `backendNotify`          | Toast notification (info / ok / error)                   |

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
