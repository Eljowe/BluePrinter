# BluePrinter (JUCE + CMake + WebView2)

A minimal JUCE audio plugin template with a WebView2 (Edge) editor and a React +
Vite frontend. Designed to be forked and customised.

The plugin currently implements a single `Gain` parameter (linear, 0..1)
passed straight to the audio buffer — a working pass-through you can replace
with real DSP.

## What's in the box

- **JUCE audio processor** (`Source/PluginProcessor.{h,cpp}`) — pass-through
  with a `Gain` parameter wired through an `AudioProcessorValueTreeState`.
- **Native fallback editor** (`Source/PluginEditor.{h,cpp}`) — a simple
  "WebView2 is not available" message used when the WebView2 runtime is
  missing.
- **WebView2 editor** (`Source/WebViewEditor.{h,cpp}`) — the main editor. It
  serves the built React app from `WebUI/dist/` via JUCE's resource provider
  and bridges parameter changes between the APVTS and the frontend.
- **React + Vite frontend** (`WebUI/`) — a single `Knob` component bound to
  the `Gain` parameter. Edit `WebUI/src/App.jsx` to build your own UI.

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

From the project root:

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

This produces `WebUI/dist/index.html` plus the hashed assets. The editor
picks them up automatically.

### 3) Hot-reload dev server (optional)

Start the Vite dev server:

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

- **Backend → frontend** — the editor's `timerCallback` (60 Hz) and the
  `parameterChanged` callback emit a `backendParameters` event whose payload
  is the current `AudioProcessorValueTreeState` snapshot. The React app
  subscribes via `window.__JUCE__.backend.addEventListener`.
- **Frontend → backend** — when a control changes, the React app calls
  `window.__JUCE__.backend.emitEvent("frontendSetParameter", { id, value })`.
  The C++ listener looks up the parameter by id in the APVTS and calls
  `setValueNotifyingHost`.
- **Initial state** — the editor injects the current parameter values via
  `withInitialisationData("parameters", ...)`; the React app reads them from
  `window.__JUCE__.initialisationData.parameters[0]`.

To add a new parameter:

1. Add it to `createParameterLayout()` in `Source/PluginProcessor.cpp`.
2. Add a matching `addParameterListener` in `BluePrinterWebViewEditor`'s
   constructor (and the matching `removeParameterListener` in the
   destructor).
3. Include the value in `makeParameterSnapshot()`.
4. Read/write it from `WebUI/src/App.jsx`.

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

This repository has no CTest tests configured. Run the standalone, drag the
`Gain` knob, and verify the level changes.

## VS Code tasks

Provided under Terminal -> Run Task:

- **Install WebUI dependencies**
- **Build WebUI**
- **Build BluePrinter Debug**
- **Rebuild BluePrinter Debug**
- **Run BluePrinter Debug**
