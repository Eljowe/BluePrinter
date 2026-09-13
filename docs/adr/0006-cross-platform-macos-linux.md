# Cross-platform: macOS and Linux

BluePrinter now targets macOS and Linux alongside Windows (superseding
[ADR-0001](0001-windows-only-webview2-ui.md)). The Windows-only decision was
revisited once the Windows safety net (CTest, CI, release path) was in place; the
platform seams proved small, and the macOS VST3/AU amp-sim ecosystem makes a
second platform worthwhile. Linux is included at a lower priority with a
documented ecosystem caveat.

## Decisions

- **macOS**: universal binary (arm64 + x86_64), minimum macOS 11. Formats: AU +
  VST3 + Standalone. **Signing/notarization is deferred** — v1 ships unsigned
  (users see a Gatekeeper warning); a paid Apple Developer account and
  notarization can follow without changing the build.
- **Linux**: Ubuntu 22.04+ x86_64 baseline. Formats: VST3 + Standalone (JUCE has
  no AU/LV2 output). Packaging: **AppImage + .deb**. The UI requires the
  **WebKitGTK 4.1** runtime (`libwebkit2gtk-4.1`), declared as a dependency and
  documented.
- **UI backend**: `juce::WebBrowserComponent` selects its backend per platform —
  `webview2` (Windows), `wkwebview` (macOS), `webkitgtk` (Linux). The local JUCE
  patch and the WebView2 cache/backend setup stay Windows-only.
- **Crash diagnostics**: Windows keeps the WER/`SetUnhandledExceptionFilter`
  path; macOS and Linux get a POSIX handler (`sigaction` + backtrace) writing the
  same `crash-info.txt` the self-heal parser reads, degrading gracefully where a
  backtrace is unavailable.
- **CI**: build + test on `windows-latest`, `macos-latest` and `ubuntu-latest`.

## Consequences

- ADR-0001's Windows-only assumptions become per-platform. "It doesn't build on
  macOS/Linux" is now a bug.
- macOS v1 is unsigned: users must right-click→Open past Gatekeeper; the
  installer/packaging must not assume notarization.
- Linux carries a `webkit2gtk-4.1` runtime dependency (heavy), and the
  third-party VST3 amp-sim ecosystem on Linux is thin — the value proposition
  there is the standalone/recorder and whatever VST3s exist, not parity with the
  Windows plugin library.
- The installer/PowerShell release scripts stay Windows-specific; macOS and Linux
  get their own packaging paths.

## Status

Accepted. Supersedes [ADR-0001](0001-windows-only-webview2-ui.md). Tracked by
ticket [0028](../tickets/0028-cross-platform-macos-linux.md) and its
implementation tickets.
