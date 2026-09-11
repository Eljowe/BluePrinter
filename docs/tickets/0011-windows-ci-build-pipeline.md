---
id: "0011"
title: "Windows CI build pipeline (GitHub Actions)"
status: in-progress
blocked_by: []
---

# Windows CI build pipeline (GitHub Actions)

## Problem

The repo has no `.github/` directory and no CI. Every change is verified only on
the maintainer's machine, so a broken build can land on the default branch and —
through `installer/build-release.ps1` — end up in a release bundle. There is no
machine-checked proof that a clean checkout builds the WebUI and both C++ targets.

## Current behaviour

- WebUI: `cd WebUI && npm install && npm run build`.
- C++: CMake configure with `-DJUCE_DIR=C:/JUCE/JUCE` (or the default), then
  `cmake --build build --config Debug|Release` for `BluePrinter_VST3` and
  `BluePrinter_Standalone`.
- All of this runs locally via VS Code tasks (`.vscode/tasks.json`); nothing runs
  automatically.

## Required change

1. Add `.github/workflows/build.yml`, triggered on push to the default branch and
   on pull requests.
2. Job on `windows-latest`:
   - checkout;
   - set up Node 18+ and run `npm ci && npm run build` in `WebUI/`;
   - obtain JUCE at a pinned ref (checkout to a cache path, or CMake
     `FetchContent` with a fixed tag) and cache it;
   - `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DJUCE_DIR=<juce>`;
   - build the Release config for `BluePrinter_Standalone` and
     `BluePrinter_VST3`.
3. Upload the standalone `.exe` and the `.vst3` bundle as workflow artifacts.
4. Cache npm and the JUCE checkout so runs stay fast.
5. Leave a clearly-marked spot to add the `ctest` step once 0012 lands (do not
   block this ticket on it).

## Acceptance criteria

- A push/PR runs the workflow and it goes green from a clean checkout.
- The workflow fails if the WebUI build or either C++ target fails.
- The built standalone and VST3 are downloadable artifacts of the run.
- No secrets are required to build (unsigned).

## Docs

`README.md` (Build/Run), `AGENTS.md` (Commands / VS Code tasks) — note that CI
mirrors the local build.

## Files

New `.github/workflows/build.yml`; possibly `CMakeLists.txt` if the JUCE
acquisition needs a cache-friendly variable.

## Out of scope

Code signing (0015), installer compilation, and release publishing — the flow
stays local until signing is sorted.
