---
id: "0028"
title: "Cross-platform support: macOS and Linux builds (deferred)"
status: needs-triage
blocked_by: []
---

# Cross-platform support: macOS and Linux builds (deferred)

## Problem

BluePrinter is Windows-only by design (see ADR-0001). Users on macOS and Linux
cannot build, install, or run it. This ticket records the plan so the effort can
start when it feels right; it is **deliberately deferred** and not ready for an
agent.

## Current behaviour (what is Windows-specific)

- **UI**: WebView2 is Windows-only. `CMakeLists.txt` sets `NEEDS_WEBVIEW2 TRUE`,
  and `WebViewEditor.cpp` uses a locally-patched JUCE
  (`WinWebView2::withAdditionalBrowserArguments` —
  `cmake/patches/juce-webview2-additional-args.patch`) to pass
  `--allow-no-sandbox-job --disable-gpu`.
- **Plugin formats**: `FORMATS VST3 Standalone` only (no AU).
- **Packaging**: Inno Setup (`installer/BluePrinter.iss`) is Windows-only; the
  release scripts (`installer/build-release.ps1`, `create-release.ps1`) are
  PowerShell + Windows-addressed.
- **Crash diagnostics**: `SetUnhandledExceptionFilter`, WER LocalDumps, and
  `%APPDATA%\Retrokielto\crash-info.txt` are Windows-only.
- **Paths/state**: `%APPDATA%` properties/settings locations, editor window-size
  persistence.
- **CI**: `.github/workflows/build.yml` is a single `windows-latest` job.

## Decisions required before implementation

1. **Targets**: macOS universal (arm64 + x86_64) and Linux x86_64? Minimum OS
   versions?
2. **Formats**: macOS = AU + VST3 + Standalone? Linux = VST3 + Standalone?
3. **Signing**: macOS notarization needs a paid Apple Developer account and has
   lead time; Linux is unsigned. Is the maintainer willing to own this?
4. **WebView backend**: WKWebView on macOS, WebKitGTK on Linux (JUCE's
   `WebBrowserComponent` supports both). What does the Linux runtime dependency
   (`webkit2gtk`) mean for packaging?
5. **Hosting relevance**: the core value is hosting third-party VST3 amp sims;
   confirm the Linux/macOS plugin ecosystem is worth the effort.
6. **Feature parity**: which features degrade or are disabled per platform
   (crash diagnostics, WER-style dumps, some MIDI device behaviour)?
7. **CI cost**: macOS runners bill at 10x (free while the repo is public).

## Proposed work breakdown (create these only once green-lit)

- **CMake per-platform**: keep `NEEDS_WEBVIEW2`/the JUCE patch Windows-guarded;
  add AU for macOS; per-platform bundle ids / plists.
- **Editor backends**: select `Backend::webview2` vs `Backend::wkwebview` /
  `Backend::webkitgtk`; stop referencing the Windows-only JUCE patch off-Windows.
- **Portable paths/state**: verify all `File::getSpecialLocation` usage; fix any
  hardcoded Windows paths (properties, settings, editor size).
- **Crash diagnostics**: portable handler (macOS `signal`/Mach exception, Linux
  `sigaction`+backtrace) or a graceful no-op.
- **Packaging**: macOS `.pkg`/`.dmg` + notarization; Linux AppImage/`.deb`;
  per-platform release scripts.
- **CI**: add `macos-latest` + `ubuntu-latest` build jobs (see 0011's workflow).
- **Docs + ADR**: supersede ADR-0001; update README install sections.

## Acceptance criteria (for the overall effort)

- A clean checkout builds the standalone + plugin formats on each supported
  platform.
- The WebView UI loads and the bridge events work in a manual smoke test on each
  platform (per AGENTS.md: verify in a real WKWebView/WebKitGTK, not headless).
- Packaged artifacts install and launch (macOS notarized where required).
- No Windows-only compile errors remain on non-Windows targets.
- CI builds all supported platforms.

## Risks / effort

- Multi-week effort; macOS notarization lead time + a paid account; Linux
  `webkit2gtk` runtime dependency; VST3 availability on Linux.
- Manual test burden multiplies — this is far safer after the CTest harness
  (0012) and CI (0011) land.

## Docs

`docs/adr/0001-windows-only-webview2-ui.md` (supersede), `README.md`
(install/runtime sections), `AGENTS.md` (build system, CI, crash diagnostics).

## Files

`CMakeLists.txt`, `Source/WebViewEditor.{h,cpp}`, `Source/PluginProcessor.{h,cpp}`,
`installer/`, `.vscode/tasks.json`, `.github/workflows/`, `README.md`,
`AGENTS.md`, `docs/adr/`.

## Deferred / blocking

**Do not start yet.** Revisit once (a) the Windows safety net and release path are
stable (0011, 0012, 0015) and (b) the decisions above are made. If green-lit, this
spec is superseded by its implementation tickets and ADR-0001 is superseded by a
new ADR.

## Out of scope

AAX/other formats; any changes to the Windows build; cloud sync.
