---
id: "0039"
title: "Update check and in-app upgrade notice"
status: needs-triage
blocked_by: ["0015"]
---

# Update check and in-app upgrade notice

## Problem

Downloading and re-installing manually is the only update path, so users miss bug
fixes. A lightweight check paired with the existing release pipeline closes that loop
without building a full auto-updater.

## Current behaviour

`installer/build-release.ps1` assembles a bundle with `SHA256SUMS.txt` and
`installer/create-release.ps1` publishes a draft GitHub release. Nothing in the app
knows a new version exists.

## Required change

1. On launch (opt-out via a persisted setting), query the GitHub releases API and
   compare the latest tag with the compiled version (parsed from `CMakeLists.txt`).
2. Show a one-time, dismissible notification with the version, release notes link and
   download link — do **not** silently install.
3. Decide the mechanism (browser/download page is the recommended first cut) and keep
   it simple; any executable the app launches must be signed.
4. Respect privacy/offline: no telemetry, no forced check; document the endpoint and
   how to disable it.

Blocked by 0015 so shipped binaries are signed and updates are trustworthy.

## Acceptance criteria

- A newer release produces one dismissible notification with version + link.
- Up-to-date, offline or rate-limited responses are silent (no error noise).
- The check can be disabled and the behaviour is documented.
- The app never executes an unsigned binary.

## Docs

`README.md`, `docs/release-checklist.md`, `AGENTS.md` (VS Code tasks/release).

## Files

`Source/PluginProcessor.h/.cpp` (timer/update check), `Source/WebViewEditor.h/.cpp`,
`WebUI/src/components/HeaderControls.jsx` / `WebUI/src/components/Notification.jsx`,
`WebUI/src/bridge.js`, `CMakeLists.txt` (version source of truth).

## Out of scope

Silent background installation; delta updates; hosting outside GitHub releases.
