# SimpleEQ (JUCE + CMake)

This project builds a JUCE plugin in these formats:

- VST3
- Standalone app

## Prerequisites

- Windows
- Visual Studio 2022 (Desktop development with C++)
- CMake 3.22+
- JUCE source at C:/JUCE/JUCE (default in CMakeLists.txt)

If JUCE is in a different location, pass JUCE_DIR during configure.

## Configure

From the project root, run:

cmake -S . -B build -G "Visual Studio 17 2022" -A x64

If JUCE is elsewhere:

cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DJUCE_DIR="C:/path/to/JUCE"

## Build

Build Debug (all default targets):

cmake --build build --config Debug

Rebuild Debug from clean:

cmake --build build --config Debug --clean-first

Build only the standalone app target:

cmake --build build --config Debug --target SimpleEQ_Standalone

Build only the VST3 target:

cmake --build build --config Debug --target SimpleEQ_VST3

## Run

Run the standalone app after building:

.\build\SimpleEQ_artefacts\Debug\Standalone\SimpleEQ.exe

Or build and launch in one PowerShell command:

cmake --build build --config Debug --target SimpleEQ_Standalone; Start-Process -FilePath ".\\build\\SimpleEQ_artefacts\\Debug\\Standalone\\SimpleEQ.exe"

You can also use the VS Code task named: Run SimpleEQ Debug

## Testing

This repository currently has no automated CTest tests configured.

You can check anyway with:

ctest --test-dir build -C Debug --output-on-failure

If no tests are registered, CTest will report that no tests were found.

Recommended manual test flow:

1. Build Debug.
2. Run the Standalone app and verify UI controls react correctly.
3. Load the VST3 in a host DAW and verify audio passes through.
4. Toggle bypass controls (LowCut, Peak, HighCut, Distortion) and verify behavior.
5. Enable analyzer and verify response/FFT display updates during playback.

## WebView UI (React + Vite)

This project now includes a WebView-based editor implementation in [Source/WebViewEditor.cpp](Source/WebViewEditor.cpp), powered by a React + Vite app in [WebUI](WebUI).

### 1) Frontend setup

From the project root:

cd WebUI
npm install

### 2) Build frontend assets for JUCE WebView

cd WebUI
npm run build

This creates: WebUI/dist/index.html

The JUCE editor tries to load this file automatically.

### 3) Optional: load a custom URL (dev server)

You can override the loaded URL with an environment variable:

set SIMPLEEQ_WEB_UI_URL=http://127.0.0.1:5173

Then run:

cd WebUI
npm run dev

And launch the plugin/standalone in the same shell session where the environment variable is set.

### 4) Build JUCE plugin/app

cmake --build build --config Debug --target SimpleEQ_Standalone
cmake --build build --config Debug --target SimpleEQ_VST3

### Notes

- Web browser support is enabled via JUCE_WEB_BROWSER=1 in [CMakeLists.txt](CMakeLists.txt).
- Initial bridge wiring from React controls to APVTS parameters is not implemented yet.

## VS Code Tasks

This workspace already includes these tasks:

- Build SimpleEQ Debug
- Rebuild SimpleEQ Debug
- Run SimpleEQ Debug

Run them from Terminal -> Run Task.
