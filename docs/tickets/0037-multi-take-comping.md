---
id: "0037"
title: "Multi-take recording and comping"
status: done
blocked_by: []
---

# Multi-take recording and comping

## Problem

The take recorder keeps exactly one pending take; a new capture invalidates it. Real
takes need several passes to choose from (or to comp the best bars), so today the
performer must save or throw away each pass before trying again.

## Current behaviour

A single pending take (`takePending` / `takeLength` / `takePeaks`), auditioned through
the take review playback path, with take overdub (0023) layering onto that one take.
The take audio lives in the shared record buffer (also used by the looper); any new
capture — take or loop — invalidates the pending take. The take state machine now
lives in the `TakeRecorder` module.

## Required change

1. Keep a bounded stack of takes from consecutive passes instead of one, with the
   existing audition UI showing them all.
2. Let the user select which take to save/export and delete individual takes.
3. Bound memory and make it explicit when older takes are dropped.
4. Reuse the take review playback path to audition each take.
5. Make take overdub target the selected take.

## Decisions (grilled)

- **Storage:** each finalized take is copied into its own `shared_ptr<AudioBuffer<float>>`
  at finalize (message thread, under the record lock). The shared record buffer stays
  the live capture scratch shared with the looper.
- **Capture semantics:** every stop appends a take. A fresh (non-Dub) take and a loop
  capture no longer invalidate the stack. Dub layers into the selected take. The stack
  clears on restart or explicit Discard / Discard all.
- **Identity:** a monotonic session integer `takeId` (never reused) keys selection,
  save/delete/overdub-target and the UI/payload.
- **Bounds:** cap 8 takes and 256 MB; drop the oldest and surface a toast.
- **Session-only:** takes vanish on restart unless saved as snippets.
- **Overdub:** on Dub start, copy the selected take back into the record buffer; capture
  the layer after it (write cursor = take length) exactly as 0023 does; wrap-mix via
  `mixOverdubLayer` into `[0, takeLength)`; copy the result back into the same `takeId`
  (refresh length and peaks). Other takes untouched.
- **Threading:** the audio thread reads a message-thread-set selected-take `shared_ptr`
  snapshot; any selection change, delete or overdub-finalize happens only while review
  playback is stopped.
- **Selection:** a new take is auto-selected; deleting the selected take selects the
  neighbour (next, else previous); changing selection or deleting the playing take stops
  playback.
- **UI:** `TakeReview` becomes a wrapping chip row (index + duration, click to select,
  per-chip delete) over the selected take's waveform/playhead, with Play/Stop, Save,
  Delete and a global Discard all; the Dub toggle targets the selection.
- **Bridge:** keep `takeLength`/`takePlaying`/`takePosition`/`takePeaks` meaning the
  selected take and `takePending` = "has any takes"; add `takes: [{ id, length }]` and
  `selectedTakeId`. `frontendSaveTake`/`frontendDiscardTake` carry `{ id }` (0 = the
  selected take; ids are >= 1); add `frontendSelectTake { id }` and
  `frontendDiscardAllTakes`. The oldest-take drop is surfaced through the existing
  `backendNotify` toast from the editor timer (no dedicated backend event).
- **Save removes the take:** saving a take to the library removes it from the stack
  (it is a snippet now), matching the old single-take behaviour.
- **Keys:** Enter saves the selected take; Delete/Backspace deletes the selected take
  with **no confirmation** (deliberate, lossy — an undo is a possible follow-up).
- **Comping** (region copy between takes) is out of scope for this cut.
- **Fixed-length take capture** does not exist (it is looper-only) and is not added.

## Agent Brief

**Category:** enhancement
**Summary:** Replace the single pending take with a bounded, selectable stack of takes —
each auditionable and individually savable — with overdub targeting the selected take.

**Current behavior:**
One pending take per session. Its audio sits in the shared record buffer; a new capture
(take or loop) invalidates it. Review playback reads the shared buffer. The UI shows a
single take with Play/Save/Discard and a Dub toggle that layers into that one take.

**Desired behavior:**
- Every stopped take is retained in an ordered, bounded stack (8 takes / 256 MB, oldest
  dropped with a user-visible notice), each with its own audio.
- Each take can be selected and auditioned individually; the selected take can be saved
  to the library (as a snippet) or deleted without disturbing the others.
- A fresh take auto-selects on finalize; deleting the selected take selects its
  neighbour; "Discard all" empties the stack.
- Dub layers the next pass into the selected take and leaves the others untouched.
- Changing selection or deleting the take that is playing stops review playback first.
- Takes are session-only unless saved; restart clears the stack.

**Key interfaces:**
- `TakeRecorder` — currently owns a single pending take (`takePending`/`takeLength`/
  `takePeaks`) and the review/overdub flags. It should instead own an ordered, bounded
  collection of takes, each carrying a session `takeId`, length, peaks and its own audio
  buffer, and expose: append-on-finalize, select by id, delete by id, delete all,
  save the selected take, and audition the selected take.
- Review playback must read the selected take's buffer, not the shared record buffer.
- `mixOverdubLayer` stays shared with the looper; overdub reuses it, then writes the
  mixed region back into the selected take.
- The transport snapshot must additionally ship `takes` and `selectedTakeId`; the
  existing selected-take scalars keep their names and meaning.
- Bridge event names must be updated in both `WebViewEditor` and `bridge.js` (the
  parity test enforces they match).

**Acceptance criteria:**
- [x] Recording three passes keeps all three, each auditionable and individually savable.
- [x] Saving or deleting one take does not disturb the others.
- [x] Discarded takes do not reappear after restart (session-only).
- [x] Count-in and take overdub still work; Dub layers into the selected take and leaves
      the others untouched.
- [x] The newest take auto-selects; deleting the selected take selects its neighbour;
      Discard all empties the stack.
- [x] At 8 takes / 256 MB the oldest is dropped and a notice is shown.
- [x] Enter saves the selected take; Delete/Backspace deletes the selected take without
      confirmation.
- [x] The bridge parity test stays green; Debug and Release standalone + VST3 build; the
      unit-test suite stays green.

## Implementation notes

- `TakeRecorder` owns the stack (one `Take { id, length, peaks, audio }` per take) and
  keeps `selectedAudio` as a `shared_ptr` snapshot the audio thread copies per render;
  the processor owns the shared record buffer + lock and computes the peaks at finalize.
- Take ids start at 1 so `0` can mean "the selected take" in the `{ id }` events.
- Save removes the saved take from the stack; Dub replaces the selected take's audio in
  place (same `takeId`).
- `Tests/test_TakeRecorder.cpp` covers append/auto-select, bounds + drop reporting,
  one-shot review, neighbour selection, overdub stage/replace, clear, and the count-in
  intent. Suite 118/118; Debug + Release standalone/VST3 build; bridge parity green.

**Out of scope:**
- Region-copy comping and crossfade comping UI.
- Fixed-length take capture (looper-only today).
- Persisting the take stack across restart.
- Per-take gain automation.

## Docs

`AGENTS.md` (takes/overdub), `README.md`, `webui-bridge` skill, `CONTEXT.md`.

## Files

`Source/TakeRecorder.h/.cpp`, `Source/PluginProcessor.h/.cpp`,
`Source/WebViewEditor.h/.cpp`, `WebUI/src/components/TakeReview.jsx`,
`WebUI/src/components/Transport.jsx`, `WebUI/src/App.jsx`,
`WebUI/src/bridge.js`, `Tests/`.

## Out of scope

Crossfade comping UI; per-take gain automation; cloud sync; persisting take audio.
