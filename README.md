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

## VS Code Tasks

This workspace already includes these tasks:

- Build SimpleEQ Debug
- Rebuild SimpleEQ Debug
- Run SimpleEQ Debug

Run them from Terminal -> Run Task.
