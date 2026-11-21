# Build, CI, packaging and release

Moved verbatim from `AGENTS.md` (ticket 0064, 2026-11-21). Rules stay in
[`AGENTS.md`](../../AGENTS.md); this file keeps the mechanics and incident history. The day-to-day
build/test commands are also in AGENTS.md "Commands".

## Build system

- CMake 3.22+ with JUCE CMake API
- JUCE source tree expected at `C:/JUCE/JUCE` (override with `-DJUCE_DIR=...`), **JUCE 8.0.14 on
  Windows plus the patch in `cmake/patches/juce-webview2-additional-args.patch`**
  (`WinWebView2::withAdditionalBrowserArguments`, used by `WebViewEditor.cpp` to pass
  `--allow-no-sandbox-job --disable-gpu`). Apply with `git -C <juce> apply cmake/patches/juce-webview2-additional-args.patch`;
  CI does this after checkout **on Windows only** — macOS/Linux use stock JUCE (the patch touches
  WinWebView2 and the call site is `#if JUCE_WINDOWS`).
- Output formats (ADR-0006): VST3 + Standalone everywhere; AU added on macOS. The WebView2 SDK
  (`NEEDS_WEBVIEW2`, `JUCE_USE_WIN_WEBVIEW2=1`) is Windows-only; macOS uses WKWebView, Linux
  WebKitGTK 4.1 (documented runtime dep).
- **ASIO**: `CMakeLists.txt` sets `JUCE_ASIO=1` in the Windows-only compile-definitions block (next
  to `JUCE_USE_WIN_WEBVIEW2=1`) so the standalone/host exposes the ASIO device type — FlexASIO and
  interfaces' native ASIO drivers enumerate from `HKLM\SOFTWARE\ASIO` (`juce_ASIO_windows.cpp`).
  JUCE 8 bundles the ASIO headers (`JUCE_ASIO_USE_EXTERNAL_SDK` stays 0), so no Steinberg SDK
  download is needed — but the resulting binary depends on those headers and must follow Steinberg's
  ASIO license terms. macOS/Linux are unaffected (`JUCE_ASIO` defaults to 0 and the define is
  `if(WIN32)`-gated).
- See `CMakeLists.txt` for full build config and install rules
- WebUI must be built separately: `cd WebUI && npm install && npm run build`

## VS Code tasks and release scripts

See `.vscode/tasks.json` for 11 pre-configured tasks:

- Install WebUI deps, Build WebUI
- Build/Rebuild BluePrinter (Debug)
- Run BluePrinter (Debug/Release)
- Build + Install + Run (Release, per-user)
- Install VST3 to Program Files (elevated — UAC copy of the built bundle; script:
  `cmake/install-vst3.ps1`)
- Build Installer (Inno Setup — compiles `installer/BluePrinter.iss` into
  `build/installer/BluePrinterSetup-1.1.0.exe`; requires Inno Setup 6.3+, installed via `winget install JRSoftware.InnoSetup`)
- Build Release Bundle (script: `installer/build-release.ps1` — builds WebUI + Release targets,
  compiles the installer with the version parsed from CMakeLists.txt passed as `/DMyAppVersion`,
  then assembles `build/release/BluePrinter-<version>/` containing the installer, LICENSE, README.md
  and SHA256SUMS.txt)
- Create GitHub Release (script: `installer/create-release.ps1` — creates the `v<version>` tag at
  the default-branch head and a **draft** release with every bundle file as an asset, all via the
  REST API; needs `$env:GH_TOKEN` with classic `repo` or fine-grained `Contents: read and write`
  scope; pass `-Published` to skip the draft)

Releasing is a fixed, ordered process: follow
[`docs/release-checklist.md`](../release-checklist.md). The version is bumped in `CMakeLists.txt`
first (the release scripts parse it). Per-platform bundles (ADR-0006):
`installer/build-macos-release.sh` (unsigned `.dmg`, universal, AU+VST3+Standalone) and
`installer/build-linux-release.sh` (`.deb` + AppImage, WebKitGTK 4.1 runtime dep); both are built
and uploaded as CI artifacts by the `macos-latest`/`ubuntu-latest` jobs.

## CI (.github/workflows/build.yml)

- **CI**: `.github/workflows/build.yml` runs on every push to `master` and every PR as three build
  jobs plus an advisory Linux ASan/UBSan test job (ADR-0006): **windows-latest** (builds the WebUI,
  checks out + patches the pinned JUCE — `JUCE_VERSION` = **8.0.14**,
  `cmake/patches/juce-webview2-additional-args.patch` — caches it, configures with **Ninja + MSVC**
  (not "Visual Studio NN YYYY", so runner-image VS bumps don't break it), installs the WebView2 SDK
  from NuGet into `JUCE_WEBVIEW2_PACKAGE_LOCATION`, builds Release standalone + VST3, uploads
  artifacts), **macos-latest** (stock JUCE, no patch/WebView2 SDK; builds AU + VST3 + Standalone)
  and **ubuntu-latest** (installs the JUCE Linux deps incl. `libwebkit2gtk-4.1-dev`, builds VST3 +
  Standalone). The three build jobs run the CTest suite (`ctest --test-dir build --output-on-failure`),
  which is built as part of `cmake --build build`; the sanitizer job runs `BluePrinterTests` only
  and is `continue-on-error` until it has a green streak. Actions are pinned to commit SHAs and the
  NuGet CLI to v6.11.1, every job has a 45-minute timeout, and Dependabot keeps the pins and the
  WebUI npm dependencies current. No secrets required (unsigned). Mirrors the local build, so a
  green run is the machine-checked equivalent of "it builds on my machine".

## Lint / format

- **Lint / format / test**: no `.clang-format`, ESLint, or Prettier config, so match the style of
  surrounding code by hand.

The CTest suite command lives in AGENTS.md "Commands"; CI runs it on every push and PR.

Keep this section in sync if a formatter or lint command is added.
