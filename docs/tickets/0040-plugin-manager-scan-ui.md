---
id: "0040"
title: "Plugin manager: scan status, browse and quarantine control"
status: done
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

## Decisions (triaged)

- **Surface:** a footer **Plugins** popover beside Diagnostics (not a new page), listing
  the cached scan metadata (`availablePlugins`). Keeps the section layout untouched.
- **Scope (first cut):** read-only listing + search + rescan + **quarantine control**.
  Favourite/hide-to-shorten-the-add-menu is explicitly deferred (out of scope below).
- **Data:** quarantine is surfaced by **file name** (`pluginQuarantine`), shipped in the
  `backendVst3Chain` snapshot as `quarantine: [fileName, …]`; each list entry is marked
  when its file name matches. Clearing calls the existing
  `clearPluginQuarantineForFile` path ("re-add from a chain to retry").
- **Progress/errors:** reuse the existing async scan (`frontendScanVst3Folder`) and its
  `backendVst3ScanProgress` stream; no new scan machinery. Listing never instantiates a
  plugin (metadata already cached).
- **Persistence:** `availablePlugins` already persists in host state, so the list
  survives restart; quarantine is the persisted `pluginQuarantine` property.

## Agent Brief

**Category:** enhancement
**Summary:** Give the user a read-only Plugin manager: browse/search the discovered VST3s,
rescan, and see/clear quarantined plugins — all metadata-only, no instantiation.

**Desired behaviour:**
- A footer **Plugins** popover lists every cached plugin (`name`, vendor, `path`) with a
  text search over name/vendor/path and a **Rescan** button (reuses the async folder scan).
- Live scan progress (`current / total` + current file) is shown while a scan runs.
- Entries whose file name is in `pluginQuarantine` are marked **quarantined** (crash-safe
  skip) with an **Allow retry** action that clears the quarantine entry.
- The list is metadata-only; scanning/listing never instantiates a plugin.

**Acceptance criteria:**
- The list matches the scanned folders and survives a restart.
- Rescan runs without blocking the UI; progress is visible.
- Quarantined plugins are clearly marked; clearing re-enables a retry.
- No plugin is loaded/instantiated during scan or listing.

**Key interfaces:**
- `PluginProcessor::getPluginQuarantineSnapshot()` → `[fileName, …]`; the chain snapshot
  ships it as `quarantine`.
- New `frontendClearPluginQuarantine { file }` → `clearPluginQuarantineForFile` + persist.
- New `WebUI/src/components/PluginManager.jsx`; render from `App.jsx` footer.

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
