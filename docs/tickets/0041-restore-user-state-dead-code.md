---
id: "0041"
title: "restoreUserState is dead code: tag names are lost and the DAW self-heal anchor is unset"
status: done
blocked_by: []
---

# `restoreUserState` is dead code

## Problem

`BluePrinterAudioProcessor::restoreUserState()` is defined but never called.
The constructor restores only `libraryFolder` inline, and
`restoreSavedPluginChains()` restores only the VST3 chains. Two pieces of state
that only `restoreUserState()` reads are therefore never restored:

1. **Snippet colour tag names** (`tagNames`, the JSON blob under the `tagNames`
   properties key written by `setTagName`/`flushTagNamePersist`). User-renamed
   colour tags reset to their built-in labels on every launch.
2. **`userStateMtimeAtLaunch`** — the launch-time properties mtime used as the
   fallback freshness anchor by `RestoreSelfHeal::plan` for hosts that have no
   `BluePrinter.settings` file (DAW hosts). It stays default/epoch, so the
   crash-quarantine anchor falls back to the properties mtime *after* the
   library-folder write instead of before it.

It also blocks 0035 (setlists would naturally live next to `tagNames` and need
the same restore path).

## Current behaviour

`restoreUserState()` exists (library folder + tag names + chains) but has no
call site. The constructor only reads `libraryFolder`; the chain block was
removed when chain restore became deferred, and the tag-name read was added to
the already-dead function.

## Required change

Call the non-chain user-state restore at construction and keep chain restore
deferred:

- `restoreUserState()` restores the library folder (auto-loading snippets), the
  `tagNames` map, and captures `userStateMtimeAtLaunch` before any write.
- It does **not** touch the VST3 chains; `restoreSavedPluginChains()` and the
  host's `setStateInformation` own that deferred restore.
- The constructor calls `restoreUserState()` instead of the inline
  folder-only restore.

## Acceptance criteria

- Renaming a colour tag survives a restart (standalone).
- The constructor no longer instantiates plugins; chain restore still happens
  one slot per message-loop turn via the deferred driver.
- `userStateMtimeAtLaunch` is set before the constructor's first properties
  write.

## Docs

`AGENTS.md` (persistence / deferred restore), `CONTEXT.md`.

## Files

`Source/PluginProcessor.h/.cpp`.

## Out of scope

- Moving `tagNames`/setlists into APVTS or the state blob.
- Changing the deferred chain-restore sequencing.

## Comments

2026-09-13 — Fixed. The constructor calls `restoreUserState()`; the function
drops its VST3-chain block (still owned by `restoreSavedPluginChains()` /
`setStateInformation`) so it can run before the device is prepared. Verified:
WebUI `npm run build` green, Debug standalone (`BluePrinter_Standalone`)
compiles, `ctest` green (BluePrinterTests + BridgeEventParity). The
rename-a-tag-survives-restart check is a manual step on Windows.
