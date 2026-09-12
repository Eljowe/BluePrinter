---
id: "0040"
title: "Plugin manager: scan status, browse and quarantine control"
status: needs-triage
blocked_by: []
---

# Plugin manager: scan status, browse and quarantine control

## Problem

Plugin discovery and health are opaque. The folder scan runs on the UI apartment,
crashed plugins disappear into the quarantine list silently, and the only place
plugins appear is each chain's add menu. Users cannot see, rescan or manage what was
found.

## Current behaviour

`Vst3Library` scans configured folders (posted to `PluginUiApartment`), the
`pluginQuarantine` property hides plugins from restore, and the chain add menu is the
only UI surface.

## Required change

1. A **Plugin manager** view listing discovered VST3s (name, vendor, path) with
   rescan/refresh and a search filter.
2. Show quarantine/skip state per entry and allow clearing an entry (the
   `clearPluginQuarantineForFile` path) with the documented "re-add to retry"
   semantics.
3. Surface scan progress and errors (the scan already runs asynchronously) so a slow
   scan looks like progress, not a hang.
4. Keep listing strictly metadata-only — never instantiate a plugin to list it
   (`findAllTypesForFile` on the apartment).
5. Optional follow-up: favourite/hide entries to shorten the add menu (decide scope;
   read-only is acceptable for the first cut).

## Acceptance criteria

- The list matches the scanned folders and survives a restart.
- Rescan runs without blocking the UI; progress and errors are visible.
- Quarantined plugins are clearly marked and clearing the entry re-enables a retry.
- No plugin is loaded/instantiated during scan or listing.

## Docs

`AGENTS.md` (Vst3Library, quarantine, plugin UI apartment), `README.md`.

## Files

`Source/Vst3Library.h/.cpp`, `Source/PluginProcessor.h/.cpp`,
`Source/WebViewEditor.h/.cpp`, new `WebUI/src/components/PluginManager.jsx`,
`WebUI/src/bridge.js`, `Tests/`.

## Out of scope

Installing plugins or their licence flows; moving/renaming plugin files.
