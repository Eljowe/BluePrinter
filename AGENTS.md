# BluePrinter Development Instructions

## Project Overview
BluePrinter is a JUCE audio plugin (VST3 + Standalone) for recording guitar takes with VST3 FX chain hosting, a React/WebView2 UI, and snippet library management. Snippets are saved as WAV files with JSON sidecars. Windows-only (WebView2 requirement).

## Build System
- CMake 3.22+ with JUCE CMake API
- JUCE source tree expected at `C:/JUCE/JUCE` (override with `-DJUCE_DIR=...`)
- Output formats: VST3, Standalone
- See `CMakeLists.txt` for full build config and install rules
- WebUI must be built separately: `cd WebUI && npm install && npm run build`

## VS Code Tasks
See `.vscode/tasks.json` for 7 pre-configured tasks:
- Install WebUI deps, Build WebUI
- Build/Rebuild BluePrinter (Debug)
- Run BluePrinter (Debug/Release)
- Build + Install + Run (Release, per-user)

## Source Code
- All C++ source in `Source/` directory
- React source in `WebUI/src/` (entry `main.jsx`, root `App.jsx`, all components in `WebUI/src/components/`)
- Entry point: `PluginProcessor.h/.cpp` (audio), `WebViewEditor.h/.cpp` (UI bridge), `App.jsx` (frontend)

## Coding Conventions
- C++: Use `#pragma once`, `#include <JuceHeader.h>`, JUCE coding style. No raw pointers for owned objects — use `std::unique_ptr` (or `std::shared_ptr` for shared snippet audio).
- Threading: Audio thread vs message thread. Use `std::atomic` for cross-thread flags. No locks in `processBlock`.
- JavaScript: React functional components with hooks. Use `bridge.js` for all C++ communication. Never hardcode event name strings.
- APVTS currently exposes only `Gain`. Metronome/BPM/count-in/MIDI-clock/MIDI-device settings are standalone user state (`juce::PropertiesFile`) + atomics, **not** APVTS parameters.

## Commands
- Build WebUI: `cd WebUI && npm install && npm run build` (outputs `WebUI/dist/`)
- Build C++ (Debug): CMake configure with JUCE_DIR, then `cmake --build build --config Debug` — see `.vscode/tasks.json` for the exact invocation.
- **Lint / format / test**: none configured. There is no `.clang-format`, ESLint, or Prettier config, and no test suite. Match the style of surrounding code by hand. If a lint/test command is added later, update this section.

## Event Naming
All C++→JS and JS→C++ events use `static constexpr const char*` in `WebViewEditor.h`. Match these exactly in `bridge.js` (`FRONTEND_EVENTS` / `BACKEND_EVENTS` / `CHAIN_IDS`). Never hardcode event name strings. See the **webui-bridge** skill for the full event listing and the add-a-new-event recipe.

## Skills
This project has four OpenCode skills configured:
- **blueprinter**: General project knowledge, architecture, conventions
- **webui-bridge**: React WebUI and C++ WebView2 bridge development
- **juce-audio**: JUCE audio plugin patterns specific to this project
- **hallmark**: Third-party anti-AI-slop design skill (from github.com/Nutlope/hallmark) for greenfield UI pages, audits, redesigns, and design extraction. Not BluePrinter-specific — triggers on UI/landing-page design requests.
