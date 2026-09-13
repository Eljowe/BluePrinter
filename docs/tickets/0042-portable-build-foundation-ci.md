---
id: "0042"
title: "Portable build foundation + macOS/Linux CI"
status: done
blocked_by: []
---

# Portable build foundation + macOS/Linux CI

## Problem

The build is Windows-only: `NEEDS_WEBVIEW2` and `JUCE_USE_WIN_WEBVIEW2` are set
unconditionally, the editor hard-selects the WebView2 backend, and CI is a single
`windows-latest` job. Non-Windows builds do not compile. This is the foundation
the rest of ticket [0028](0028-cross-platform-macos-linux.md) depends on.

## Current behaviour

- `CMakeLists.txt` sets `NEEDS_WEBVIEW2 TRUE` and `JUCE_USE_WIN_WEBVIEW2=1`
  unconditionally; `FORMATS VST3 Standalone` (no AU); install rules already sit
  inside `if(WIN32)`.
- `WebViewEditor.cpp` builds the WebView2 cache dir and backend inside
  `#if JUCE_WINDOWS`, but still calls the local JUCE patch's
  `withAdditionalBrowserArguments` — that call is inside the Windows guard.
- Windows-only crash code is already `#ifdef JUCE_WINDOWS`-guarded.

## Required change

1. Guard `NEEDS_WEBVIEW2` and `JUCE_USE_WIN_WEBVIEW2` to Windows only; do not
   apply the `juce-webview2-additional-args.patch` requirement off-Windows.
2. Add `AU` to `FORMATS` on macOS (AU + VST3 + Standalone); Linux stays VST3 +
   Standalone.
3. Select the `WebBrowserComponent` backend per platform (`webview2` /
   `wkwebview` / `webkitgtk`) and use a per-platform user-data cache folder.
4. Audit for Windows-only assumptions: `File::getSpecialLocation` usage,
   `PluginProcessor`/`RestoreSelfHeal` paths, install rules, `.vscode` tasks.
5. Extend `.github/workflows/build.yml` with `macos-latest` and `ubuntu-latest`
   jobs that mirror the Windows job (build standalone + formats, run `ctest`),
   installing `libwebkit2gtk-4.1-dev` on Ubuntu. Leave the Windows job unchanged.

## Acceptance criteria

- [ ] A clean checkout configures and builds standalone + VST3 on macOS
      (universal, macOS 11+) and Linux (Ubuntu 22.04+); AU builds on macOS.
- [ ] The WebView UI loads and bridge events work in a manual smoke test on each
      platform (real WKWebView / WebKitGTK, not headless).
- [ ] `ctest` is green on all three CI jobs; the Windows job is unaffected.
- [ ] No Windows-only compile errors remain on non-Windows targets.
- [ ] The JUCE WebView2 patch is only required on Windows.

## Docs

`README.md` (build prerequisites per platform), `AGENTS.md` (build system, CI),
`docs/adr/0006-cross-platform-macos-linux.md`.

## Files

`CMakeLists.txt`, `Source/WebViewEditor.{h,cpp}`, `.github/workflows/build.yml`,
`.vscode/tasks.json`, `README.md`, `AGENTS.md`.

## Out of scope

Packaging/installers (0044, 0045) and portable crash diagnostics (0043).

## Comments

2026-09-13 — Implemented the platform plumbing:

- `CMakeLists.txt`: `BLUEPRINTER_FORMATS` adds AU on macOS; `NEEDS_WEBVIEW2` and
  `JUCE_USE_WIN_WEBVIEW2=1` are Windows-only.
- `WebViewEditor.cpp`: the WebView2 backend/cache/`withAdditionalBrowserArguments`
  options are `#if JUCE_WINDOWS`; macOS/Linux use JUCE's default backend.
- `PluginProcessor::createEditor`: the `areOptionsSupported` probe only forces
  the WebView2 backend on Windows, so WKWebView/WebKitGTK aren't rejected.
- `.github/workflows/build.yml`: added `macos-latest` and `ubuntu-latest` jobs
  (WebKitGTK 4.1 dev package, stock JUCE, build + ctest). Windows job unchanged.
- Docs: AGENTS.md build/CI + README.md project overview updated for ADR-0006.

Verified locally: Windows Debug standalone + tests build and `ctest` is green
(2/2). **CI is green on all three jobs** (windows-latest, macos-latest,
ubuntu-latest — run 34777658731).

Two CI-only fixes landed along the way: the Ubuntu job needed the GTK 3 +
fontconfig dev headers (juce_gui_extra includes `gtk/gtk.h`), and the target
needed `NEEDS_WEB_BROWSER TRUE` so JUCE links the Linux WebKitGTK/GTK include
dirs (the manual `-DJUCE_WEB_BROWSER=1` was removed — JUCE sets it from the
property and it was being redefined).

The manual UI smoke test per platform (real WKWebView / WebKitGTK) is still
required and is a human step.
