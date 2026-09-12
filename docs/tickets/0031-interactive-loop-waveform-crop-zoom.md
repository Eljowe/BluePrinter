---
id: "0031"
title: "Interactive loop waveform: drag crop handles and zoom"
status: ready-for-agent
blocked_by: []
---

# Interactive loop waveform: drag crop handles and zoom

## Problem

The loop waveform is already displayed (`Waveform.jsx`, used by `Looper.jsx`,
`TakeReview.jsx`, `SnippetCard.jsx`), but the crop region is only adjustable through
small whole-beat steppers. On a longer loop the user cannot see where a beat sits or
grab a crop edge directly, so trimming is fiddly and blind.

## Current behaviour

`Waveform.jsx` renders a static `<svg>` polyline from peaks. `Looper.jsx` overlays
`looper-crop-left` / `looper-crop-right` dimming and drives `frontendSetLoopCrop`
from whole-beat steppers only. The peaks already cover the **full** loop (crop regions
included) at 256 buckets, so crop shading greys the real audio — a stable context for
direct manipulation.

## Decisions (grilled)

- **Granularity:** whole beats, reusing the existing `frontendSetLoopCrop { startBeats,
  endBeats }` payload. No sub-beat crop.
- **Zoom:** pointer-wheel / control zoom + pan, rendering a sub-range of the existing
  256-bucket peaks. Raising peak resolution in C++ is a possible follow-up, not part of
  this ticket.
- **Scrub / click-to-preview:** out of scope. The playhead stays a read-only indicator.
- **Steppers stay:** the numeric crop steppers remain as the precise/keyboard path.
- **Window-move:** dragging the dimmed middle to slide the crop window is out of scope.
- **Constraints:** handles snap to whole beats, cannot cross (at least one beat is kept),
  and clamp to the same bounds the steppers use.
- **Disabled while recording / counting in / overdubbing.**
- **Accessibility:** each handle is keyboard-focusable (arrow keys move one beat,
  Home/End jump to the extremes) and announces the start/end beat.

## Agent Brief

**Category:** enhancement
**Summary:** Make the loop waveform directly editable — draggable, keyboard-operable
crop handles plus zoom/pan — while keeping the numeric steppers and payload unchanged.

**Current behavior:**
The loop waveform is a static SVG polyline; the crop region is set only by whole-beat
steppers, with dimming overlays showing the trimmed ranges. There is no direct
manipulation, no zoom, and no keyboard affordance on the waveform.

**Desired behavior:**
- Draggable start/end crop handles sit on the waveform at the current crop boundaries,
  snapped to whole beats. Dragging updates the crop exactly as the steppers do, and the
  change is audible on the next loop cycle.
- Handles cannot cross (at least one beat remains) and clamp to the same limits the
  steppers enforce.
- Wheel/button zoom plus pan let the user resolve individual beats on a long loop and
  reset to the full view.
- Each handle is focusable: arrow keys move it one beat, Home/End jump to the extremes,
  and the start/end beat is announced.
- The numeric steppers stay and remain in sync with the handles.
- Editing is disabled while recording, counting in, or overdubbing.
- No change to the crop payload, the loop peak data, or the existing overlays, ruler and
  playhead.

**Key interfaces:**
- Reuse the existing whole-beat crop event (`frontendSetLoopCrop { startBeats, endBeats }`)
  and the existing crop state — no C++ payload or sample-math change.
- `Waveform` gains a crop-region / handle overlay layer (or is wrapped by one) rather
  than introducing a second renderer.
- Zoom/pan is frontend-only view state.

**Acceptance criteria:**
- [ ] Dragging either handle changes the crop identically to the matching stepper and is
      heard on the next loop cycle.
- [ ] Handles snap to whole beats, cannot cross, and clamp to the stepper limits.
- [ ] Zoom/pan makes individual beats resolvable on a long loop and can reset to full view.
- [ ] Keyboard users can focus a handle, move it by a beat, jump to an extreme, and hear
      the announced start/end beat.
- [ ] Editing is disabled during recording, count-in and overdub.
- [ ] The steppers and handles stay in sync; existing overlays, ruler and playhead are
      unchanged and no visual regression is introduced.

## Docs

`webui-bridge` skill (component/ARIA patterns), `AGENTS.md` (looper crop).

## Files

`WebUI/src/components/Waveform.jsx`, `WebUI/src/components/Looper.jsx`,
`WebUI/src/styles.css`.

## Out of scope

Sub-beat crop; scrub / click-to-preview playback; dragging the crop window; raising peak
resolution in C++ (possible follow-up); fade/crossfade editing.
