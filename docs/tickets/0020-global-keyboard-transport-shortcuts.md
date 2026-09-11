---
id: "0020"
title: "Global keyboard transport shortcuts"
status: done
blocked_by: []
---

# Global keyboard transport shortcuts

## Problem

Starting and stopping a take or a loop capture requires clicking transport
buttons. A guitarist holding an instrument cannot easily reach the mouse, so the
core record workflow is not hands-free.

## Current behaviour

Recording/looping is driven by `frontendStartRecording`,
`frontendStopRecording`, `frontendSetLooperRecording`, `frontendSetLooperPlaying`,
`frontendSaveTake`, and `frontendDiscardTake`, all from button clicks. The only
keyboard handlers are per-field Enter-to-blur and the tab bar arrow keys.

## Required change

1. Add an app-level `keydown` handler that maps keys to the existing transport
   events — proposed defaults: `Space` start/stop capture (take record in the Take
   tab, loop record in the Loop tab), `Enter` save the pending take, `Esc` stop
   playback / discard confirmation.
2. **Ignore the handler while focus is in an `input`, `textarea`, or
   `contenteditable`** so typing a take name/notes never triggers transport.
3. Show a small shortcut hint or help popover documenting the bindings.
4. Reuse existing bridge events; do not add new backend events unless necessary.

## Acceptance criteria

- Shortcuts work in both the Take and Loop tabs and match the visible action.
- No shortcut fires while typing in name/comments/notes fields.
- Existing Enter-to-blur and tab-arrow behaviour is preserved.
- Bindings are discoverable in-app.

## Docs

`AGENTS.md` (UI / events), `webui-bridge` skill.

## Files

`WebUI/src/App.jsx`, relevant components, `WebUI/src/styles.css`.

## Out of scope

User-remappable keyboard bindings (start with fixed defaults); MIDI control (0021).

## Depends on / coordination

Coordinate with 0017 (focus management and keyboard nav).
