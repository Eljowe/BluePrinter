---
id: "0016"
title: "In-app crash diagnostics report flow"
status: in-progress
blocked_by: []
---

# In-app crash diagnostics report flow

## Problem

Crash diagnostics already exist — `%APPDATA%\Retrokielto\crash-info.txt` written by
the unhandled-exception filter, plus the WER LocalDumps instructions in `AGENTS.md`
— but there is no way for a user to surface them from inside the app. Support
depends on the user manually finding scattered files, and `crash-info.txt` can be
overwritten before it is ever read.

## Current behaviour

`setCrashOp` / `getCrashOp` write the current restore op; `chainRestoreCrashed`,
`pluginQuarantine`, and `lastPluginLoadOp` live in the properties file. The user
has no UI for any of it.

## Required change

1. Add a "Diagnostics" affordance (e.g. in a Settings/About area, or a Notification
   action after a restore failure) offering **Copy diagnostics** and **Open
   diagnostics folder**.
2. The copied bundle should include: app version, OS/build, `crash-info.txt`,
   recent `lastChainRestoreError`, the quarantine list, and a settings summary —
   with no user audio or personal file paths beyond what is needed to diagnose.
3. Provide the exact elevated WER LocalDumps command for users who want full dumps.
4. Rotate `crash-info.txt` (keep the last N) so a recent crash is not lost.

## Acceptance criteria

- One click yields a shareable diagnostics blob (clipboard or a file the user can
  attach to a report).
- The bundle contains no audio and no secrets.
- A restore failure produces a visible path to the diagnostics without digging in
  `%APPDATA%`.
- Behaviour and file format are documented.

## Docs

`AGENTS.md` (crash diagnostics section), `README.md` (troubleshooting).

## Files

`Source/PluginProcessor.h/.cpp`, `Source/WebViewEditor.h/.cpp`,
`WebUI/src/components/` (new diagnostics panel or notification action),
`WebUI/src/bridge.js`, `Source/WebViewEditor.h` (new event constant).

## Out of scope

Automatic upload/telemetry or a third-party crash-reporting SDK.
