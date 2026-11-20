---
id: "0061"
title: "Find scales from selected notes"
status: done
blocked_by: []
---

# Find scales from selected notes

## Problem

The Chords tool is scale-first: the user must choose a key and scale, then
inspect its notes and chord suggestions. A songwriter often starts from a
handful of notes they hear and wants the reverse: which scales contain those
notes, with the ability to inspect each candidate without losing the selection.

## Solution

Add a **Chords / Find scales** view switch inside the existing Chords popover
(no new header tool). In Find scales mode:

- Selecting two or more pitch classes on the piano or fretboard produces a
  ranked list of every root + scale pair from the five existing scales
  (12 roots × 5 types) that contains them.
- Scales containing all selected notes are listed first; ties prefer the
  tighter scale, then catalogue order, then root.
- If no scale contains every note, the closest fits (one missing note) are
  listed instead.
- Selecting a suggestion highlights that scale's notes on both instruments
  while leaving the selected notes untouched. Selecting another suggestion
  swaps the preview; selecting the active one or **Clear scale** removes it.
- The selected-note/chord readout, Clear notes, and the piano/fretboard
  selection model are unchanged.

## Decisions

1. The switch is a two-button segmented control inside Chords; the header tool
   remains **Chords**. Session state gains `view: "chords" | "scales"` and
   `previewScale: { root, scaleId } | null`.
2. Reverse lookup is pure frontend logic (`findScaleMatches`) reusing
   `SCALES`, `scaleNotes` and `rootName`; no C++ or bridge event is added.
3. Ranking: matched count descending, scale size ascending, `SCALES` order,
   root pitch class. This puts full fits first and prefers a five-note
   pentatonic over a seven-note scale when both contain every selected note.
4. Fewer than two selected notes is too broad; the UI prompts for more notes
   instead of listing all 60 scales.
5. Full fits suppress partial fits. When none exist, one-missing scales are
   shown; if even those do not exist, the remaining partial matches are shown.
6. Scale suggestions are independently scrollable so a long list never pushes
   the instruments out of view.
7. Preview spelling follows the preview scale; selected notes/chord naming use
   the same spelling context. Clearing notes also clears the scale preview.
8. Session-only: nothing is written to `localStorage` or host state.

## Testing

- Pure `findScaleMatches` tests in `Tests/check-chord-theory.mjs`: no/one note,
  C–E–G full fits for C Major and A Natural minor, matched-count ordering,
  tighter full fits first, and chromatic partial-only behaviour.
- WebUI production build and existing `ChordTheory`/`ScaleTheory`/
  `BridgeEventParity` checks.
- Headless Chrome smoke test: C–E–G in Find scales shows nine full-fit scales;
  selecting C Major highlights it, selecting A Natural minor swaps the preview
  and the readout.

## Out of Scope

- Custom scales, modes or tuning systems beyond the five existing scales.
- Chord-scale relationship teaching, progression generation or saving results.
- Audio/MIDI note input, audition, or persistence.
- Changing the separate header Scales tool.

## Results

Implemented. `WebUI/src/chords.js` gained `findScaleMatches`; `Chords.jsx` now
has the Chords/Find scales switch, full-fit and closest-fit groups, a
scrollable scale rail, preview highlighting on both instruments, and a Clear
scale action. For C–E–G the list contains nine full fits (C/A pentatonics,
A blues, C/F/G major and D/E natural minor). Selecting C Major then A Natural
minor swaps the preview and bottom summary without disturbing the C–E–G
selection. Tests and the WebUI build pass; verified in headless Chrome.
