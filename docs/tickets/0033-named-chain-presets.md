---
id: "0033"
title: "Named VST3 chain presets"
status: ready-for-agent
blocked_by: []
---

# Named VST3 chain presets

## Problem

A chain's plugin set can only be saved as part of the whole plugin state. There is no
way to store a named rig ("Clean amp", "Fuzz lead", "Ambient pad") and recall it on a
chain, so users rebuild the same chain by hand or reload an unrelated project.

## Current behaviour

`makeChainState` / `applyChainState` serialize and restore the entire chain list,
including per-slot state blobs (per-chain state comes from `chain->getChainState()`).
Plugin discovery lives in `Vst3Library`. User bookkeeping lives in the XML properties
file (`pluginChains`, `pluginQuarantine`, `tagNames`). There is no per-chain preset
concept.

## Decisions (grilled)

- **Storage:** a `chain-presets/` folder of one JSON file per preset under the app-data
  dir (`%APPDATA%\Retrokielto\`), not inside `BluePrinter.properties` — presets are
  portable user content, and one-file-per-preset makes rename/delete a file op.
- **Contents:** plugin file paths + order + per-slot bypass + saved state blobs, plus
  chain output volume/mute and the MIDI toggle/channel filter. **Excludes** the chain
  id/name, the input mask and the record-on-capture flag (those are project wiring, not
  the rig).
- **Apply semantics:** loading **replaces** the target chain's slots, with a confirm
  when the chain is non-empty. (Append/stack is a different feature.)
- **UI:** a per-chain card menu with Save as…, Load, and a preset manager
  (rename/delete). **Save is disabled while the chain has pending (still-restoring)
  slots.**
- **Missing/quarantined plugins:** skip them and **report a list** (partial load); the
  working part still loads. Quarantine is honoured.
- **Name collision:** confirm before overwriting an existing preset name.
- **Non-negotiables:** load goes through the deferred one-slot-per-turn restore path
  (never synchronous instantiation), the same-chain duplicate guard applies, and an
  unknown field / version mismatch warns rather than half-applying silently.

## Agent Brief

**Category:** enhancement
**Summary:** Let a user save a chain's plugin rig as a named preset and load it into any
chain, reusing the existing deferred load and persistence machinery.

**Current behavior:**
Chains are only serialized as part of the whole plugin state bundle. There is no named,
reusable, per-chain preset and no UI to save/load/manage one.

**Desired behavior:**
- Saving captures the chain's plugins (file paths, order, per-slot bypass, saved state
  blobs), its output volume/mute and its MIDI toggle/channel filter — not its id, name,
  input mask or record flag.
- Presets persist as individual JSON files in a chain-presets folder and survive restart;
  they can be renamed and deleted.
- Loading a preset into a chain replaces its slots, confirming when the chain is not
  empty. Loading always happens through the deferred path (one slot per message-loop
  turn, state applied on the next turn) and never blocks the message thread.
- Plugins that are missing/uninstalled or quarantined are skipped, and the skipped names
  are reported; the rest of the rig still loads.
- The same-chain duplicate guard rejects a preset that would duplicate a plugin file
  already in the chain.
- Saving is unavailable while the target chain still has pending slots. Overwriting an
  existing preset name requires confirmation. An unknown field or version mismatch warns
  instead of silently half-applying.

**Key interfaces:**
- Reuse the per-chain serialization shape (`getChainState()`) as the preset payload; add
  a distinct on-disk file per preset.
- Preset load feeds the existing deferred restore driver (the same one used by state
  restore), so quarantine, timeouts and the plugin-load progress/notification path apply.
- A new set of frontend→backend events for save / load / rename / delete, registered in
  the shared event constants and mirrored in the WebUI bridge (parity test must pass).
- The chain snapshot already exposes per-chain `pending`, which gates the Save control.

**Acceptance criteria:**
- [ ] Save + load reproduces a chain's plugins (order, bypass, parameter values) after a
      restart, on any chain.
- [ ] Loading never blocks the message thread and honours plugin quarantine.
- [ ] A preset that duplicates a plugin file already in the chain is rejected with a
      clear message.
- [ ] A missing/uninstalled plugin is skipped and reported; the remaining slots load.
- [ ] Loading into a non-empty chain requires confirmation; Save is disabled while the
      chain has pending slots.
- [ ] Presets are stored one-per-file, survive restart, and can be renamed and deleted.
- [ ] Unknown fields / version mismatch produce a warning, not a silent partial apply.

## Docs

`AGENTS.md` (chain persistence, deferred restore, quarantine), `README.md`.

## Files

`Source/PluginProcessor.h/.cpp`, `Source/PluginChain.h/.cpp`, `Source/Vst3Library.h/.cpp`,
`Source/WebViewEditor.h/.cpp`, `WebUI/src/components/PluginChain.jsx`,
`WebUI/src/bridge.js`.

## Out of scope

Sharing presets online; per-slot presets; cross-chain preset sync; append/stack loading.
