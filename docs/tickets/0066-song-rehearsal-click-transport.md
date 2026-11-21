---
id: "0066"
title: "Song rehearsal: click-only transport with a live section/chord highlight"
status: in-progress
blocked_by: ["0065"]
---

# Song rehearsal: click-only transport with a live section/chord highlight

## Problem

The Song tab plans the arrangement and records sections, but there is no way to
rehearse the plan: the metronome only runs through a count-in/capture or the
free-running MIDI clock, and nothing shows where you are in the song. A player
practising a demo has to start a throwaway capture to get a click, and the
chord chart sits still while the arrangement moves.

## Current behaviour

- `metronomePosition` is the continuous beat clock. It advances during a
  count-in/capture and, when the header MIDI clock is on and free-running, the
  clock advances it itself and sounds the click (`PluginProcessor.cpp`, the
  "MIDI clock output" block).
- `backendTransport` ships `transportPosition` (capture position) but not the
  metronome position, and there is no practice run state.
- The Song tab (0065) has sections, chords, an arrangement strip and the
  record/attach flow. It has no transport of its own.
- `songModel.js` has the bar/section math and `buildChordCatalog`; it has no
  position-to-section mapping.

## Design decisions

1. Rehearsal is a **click-only transport**, not song playback (ADR-0009). It
   runs the existing metronome over the song's arrangement and highlights the
   current section/bar/chord; it never plays the linked recordings.
2. It reuses `metronomePosition` and the existing free-run click path. A new
   `practiceRunning` atomic makes the position advance and the click sound
   without the MIDI clock. `practicePosition` is the same position, published in
   the transport snapshot.
3. **Count-in**: when the take count-in is on (`transport.countInBeats > 0`),
   rehearsal starts with **one full bar** of click (the lead-in), then the song
   starts on the next downbeat. The lead-in is a whole bar so the meter accents
   stay aligned. The frontend shows the count-in beat.
4. **Start points**: *Rehearse from here* starts at the selected section;
   *Rehearse song* starts at the top. Both play to the end of the song and stop
   there. Section looping is out of scope.
5. **Exclusivity**: starting a capture stops practice; starting practice stops
   snippet playback, take review, loop playback and melody audition (they are
   not synced to the click). Practice is refused during a capture. The editor
   destructor stops practice.
6. The click obeys the master `metronomeEnabled` toggle and the click sound
   settings; it is not gated by `clickDuringCapture`.
7. Session-only: no host-state key, no persistence. `Esc` and the Stop button
   end it.
8. No new backend event: one new frontend event and two snapshot fields.

## Required change

### Backend

`Source/PluginProcessor.h/.cpp`:

- `std::atomic<bool> practiceRunning { false };`
- `int64_t getMetronomePosition() const` (the current `metronomePosition`).
- `void setPracticeRunning (bool enabled, int64_t startSample = 0);`
  - enable: refuse while `takeRecorder.isActive()` or `looper.isActive()` /
    `looper.isOverdubCapture()`; stop snippet playback (`stopPlayback`), take
    review (`setTakePlayback(false)`), loop playback (`setLooperPlaying(false)`)
    and melody audition (`setSnippetMelodyPlayback(..., false)`); set
    `metronomePosition` to `max(0, startSample)`; set the flag; notify
    `transportChanged`.
  - disable: clear the flag if it was set; notify.
- Clear `practiceRunning` (if set) in the entry points that take over the
  transport: `startRecording`, `setLooperRecording(true)`,
  `startPlayback`, `setTakePlayback(true)`, `setLooperPlaying(true)`,
  `setSnippetMelodyPlayback(..., true)`, and `releaseResources`.
- The free-run block becomes:
  ```cpp
  const bool clockEnabled = clockRunning.load();
  const bool practiceEnabled = practiceRunning.load();
  const bool freeRan = (clockEnabled || practiceEnabled)
      && metronomePosition.load() == blockStartMetronomePos;
  if (freeRan)
  {
      metronomePosition.store (blockStartMetronomePos + numSamples);
      clockAdvancedThisBlock = true;
      if (metronomeEnabled.load() && (clockEnabled || practiceEnabled))
          metronomePlayer.render (buffer, blockStartMetronomePos, numSamples,
                                  clickBuffer, accentClickBuffer, subClickBuffer);
  }
  ```
  Keep the MIDI pulse rendering gated on `clockRunning` only, so practice never
  sends MIDI. Update the block comment.

`Source/WebViewEditor.h/.cpp`:

- New event `frontendSetPracticeRunning` (`{ enabled, startSample? }`) in the
  static event list; the handler calls `setPracticeRunning`.
- The transport snapshot ships `practiceRunning` (bool) and `practicePosition`
  (double samples).
- The editor destructor calls `setPracticeRunning(false)` so closing the window
  cannot leave a runaway click.

`WebUI/src/bridge.js`: add `setPracticeRunning`.

### Frontend pure model

`WebUI/src/songModel.js`:

- `rehearsalAt(song, positionSamples, { startSample = 0, leadInSamples = 0, barSamples = 0 })`
  maps the position **relative to the run's start sample**:
  - `{ phase: "idle" }` when the song/bar math is invalid;
  - `{ phase: "countin", countInBeat, sectionIndex, barInSection: 0, barInSong: startBar, chord: null }`
    while `positionSamples - startSample < leadInSamples`, where `sectionIndex`
    is the section that contains `startSample / barSamples` (the section the
    run started in);
  - `{ phase: "done", ...last position }` when
    `floor(startSample / barSamples) + floor((relative - leadInSamples) / barSamples)`
    is past the total bars;
  - `{ phase: "playing", sectionIndex, barInSection, barInSong, chord }`
    otherwise. `chord` is the last chord event at or before `barInSection` (the
    held chord), or `null`.
- `rehearsalStartSample(song, sectionIndex, barSamples)` - the section's first
  bar in samples (the lead-in bar's first beat), clamped to >= 0. The song
  starts one bar later at that sample plus `leadInSamples`.
- Tests in `Tests/check-song-model.mjs`: count-in beat, first bar, a section
  boundary, a held chord across bars, the end/done phase, an empty song, a
  later-section run (count-in on its first bar, playing on the next downbeat,
  done at the absolute end), and `rehearsalStartSample` for the top and a later
  section.

### Frontend Song tab

`WebUI/src/components/Song.jsx`:

- Read `transport.practiceRunning` / `practicePosition`,
  `transport.recordingSampleRate`, `transport.countInBeats` and
  `transport.metronomeEnabled`.
- `barSamples = barSamples(song.bpm, { numerator, denominator }, sampleRate)`;
  `leadInSamples = transport.countInBeats > 0 ? barSamples : 0`.
- A **rehearsal bar** in the Song tab:
  - *Rehearse from here* (selected section) and *Rehearse song* (top), disabled
    while capturing or when `barSamples <= 0`; a *Stop* button while running.
  - While running: the current section name, `bar n / N`, the current chord
    (symbol + name), and the count-in beat during the lead-in. A hint when the
    click is off ("Turn the click on in the sync strip").
  - An effect maps `practicePosition` through `rehearsalAt` and emits
    `setPracticeRunning { enabled: false }` once when the phase becomes `done`.
  - The start sample is stored in session state when a run starts (the Song tab
    unmounts on a tab switch, so it is kept in `sessionStorage` and restored on
    remount) and passed to `rehearsalAt` as `startSample`.
- Highlight the current section in the arrangement strip (`is-current`, distinct
  from `is-selected`) and the current chord cell (`is-current`), and show the
  big chord readout.
- Starting practice stops playback (the backend does it); starting a capture
  stops practice (the backend does it). Do not block the record buttons on
  practice; the backend clears it.
- `Esc` (App handler) stops practice: add
  `if (transport.practiceRunning) emit(setPracticeRunning, { enabled: false })`.
- Update the header Keyboard popover: Esc stops playback **and rehearsal**.

### App wiring

- Add `practiceRunning: false` / `practicePosition: 0` to
  `readInitialTransport()` defaults and the transport reducer (the reducer
  rebuilds field by field; a missing field is silently dropped).
- Add an App-level safety effect: while `practiceRunning`, compute
  `bar = barSamples(activeSong.bpm, activeSong, recordingSampleRate)`,
  `lead = countInBeats > 0 ? bar : 0`, `end = lead + totalBars(activeSong) * bar`
  (the same absolute end for any start section) and emit
  `setPracticeRunning { enabled: false }` once when `practicePosition >= end`.
  The Song tab's own stop only runs while that tab is mounted; this makes the
  end stop hold from Take/Loop too (the backend's disable is idempotent).
- The Song tab already receives `transport`; no new props.

### Docs

- `docs/adr/0009-rehearsal-is-a-click-not-a-sequencer.md`: the decision above.
- `AGENTS.md`: the Song builder section (rehearsal, practice flags, free-run
  click, snapshot fields, event), and the free-run/MIDI clock wording.
- `CONTEXT.md`: the **Rehearsal** term and the snapshot fields.
- `.opencode/skills/webui-bridge/SKILL.md`: the event + snapshot fields.
- Ticket 0066 `in-progress` with an implementation comment; README row. Do not
  mark done without the manual real-WebView2 smoke test.

## Acceptance criteria

- From the Song tab, *Rehearse from here* / *Rehearse song* start the click at
  the selected section/top with the one-bar count-in when the count-in is on.
- The arrangement block, bar counter, chord cell and big chord readout follow
  the position; the count-in beat shows during the lead-in; the run stops at the
  end and the UI clears.
- Stop and Esc end practice; starting a take/loop capture ends it; starting
  practice ends any snippet/take/loop/melody playback; practice is refused
  during a capture.
- The click is silent when the master click is off; practice still advances.
- No capture/print/audio-path change, no host-state key, no new backend event;
  bridge parity and the tests stay green.

## Files

New: `docs/adr/0009-rehearsal-is-a-click-not-a-sequencer.md`,
`docs/tickets/0066-song-rehearsal-click-transport.md`.

Touched: `Source/PluginProcessor.h/.cpp`, `Source/WebViewEditor.h/.cpp`,
`WebUI/src/bridge.js`, `WebUI/src/App.jsx`, `WebUI/src/songModel.js`,
`WebUI/src/components/Song.jsx`, `WebUI/src/styles.css`,
`WebUI/src/components/PracticeTools.jsx`, `Tests/check-song-model.mjs`,
`AGENTS.md`, `CONTEXT.md`, `.opencode/skills/webui-bridge/SKILL.md`,
`docs/tickets/README.md`.

## Out of scope

- Playing the linked recordings in sequence, a timeline, a mixer.
- Section looping, tempo changes mid-run, a footswitch.
- Persisting practice state or the position.
- Any new audio path, offline render or capture geometry.

## Comments

### 2026-11-21 — Implementation (branch `feature/0066-song-rehearsal`)

Built as specified. What landed:

- **Backend** (`PluginProcessor.h/.cpp`): session-only `practiceRunning` atomic,
  `getMetronomePosition()` / `isPracticeRunning()` / `setPracticeRunning(bool,
  int64_t)`. Enable refuses during a take/loop capture and stops snippet
  playback, take review, loop playback and the melody audition before storing
  `max(0, startSample)` and setting the flag; disable is idempotent (notifies
  only when it was set). The free-run block is now
  `(clockRunning || practiceRunning) && position unchanged`, sounds the click
  when `metronomeEnabled` and keeps the MIDI pulse path gated on `clockRunning`
  alone. `startRecording`, `setLooperRecording(true)`, `startPlayback`,
  `setTakePlayback(true)`, `setLooperPlaying(true)`,
  `setSnippetMelodyPlayback(..., true)` and `releaseResources` clear it.
- **Bridge**: `frontendSetPracticeRunning { enabled, startSample? }`;
  `practiceRunning` + `practicePosition` in `makeTransportSnapshot`; the editor
  destructor stops practice after `removeListener` so the notify can't re-enter
  the dying editor.
- **Frontend model**: `rehearsalAt` (countin / playing / done / idle, held chord
  via the last event at or before the bar, mapped relative to the run's
  `startSample`) and `rehearsalStartSample` (the section's first bar), covered
  by new `SongModel` cases (count-in beats, first bar, section boundary, held
  chords, done, empty song, start samples).
- **Song tab**: rehearsal bar with *Rehearse from here* / *Rehearse song* /
  *Stop rehearsal*, a section + bar (`bar n / N` plus the song bar) readout, a
  big chord readout, the count-in beat and a click-off hint; `is-current`
  highlights on the arrangement block, section row and chord cell; a one-shot
  ref/effect emits the stop at `done`. The run starts at the section's first
  bar, which is the **one full bar** lead-in whenever `countInBeats > 0`
  (`leadInSamples = barSamples`), so the song starts on the next downbeat.
- **App**: both transport fields in `readInitialTransport()` and the 30 Hz
  reducer, `Esc` stops practice, Keyboard popover updated.
- **Docs**: ADR-0009, AGENTS.md (Song builder + MIDI clock + Esc), CONTEXT.md
  (Rehearsal term + invariant), the webui-bridge skill.

Verified: Release build of Standalone/VST3/Tests, `ctest` 5/5
(`BluePrinterTests` 237/237), WebUI `vite build`, bridge parity 116 frontend /
7 backend, `SongModel` green, `git diff --check` clean.

Not done: the manual real-WebView2 smoke test (start from a section and from
the top, count-in beat, click off still advancing, the stop at the end, Esc,
capture pre-empting rehearsal). Ticket stays `in-progress` until that passes.

### 2026-11-21 — Review pass: count-in integration, take-melody pre-empt, end stop from any tab

Three fixes from review (build/tests were already green):

1. **The count-in bar was skipped and later-section runs mapped from bar 0.**
   The old `rehearsalStartSample` added `leadInSamples` to the start and
   `rehearsalAt` subtracted it from the absolute position, so the start position
   itself was already one bar in: the lead-in never played, the count-in never
   showed, and a later-section start was read as bar 0. Now the run starts at
   the **section's first bar** (`rehearsalStartSample(song, index, barSamples)`
   = `sectionStartBar * barSamples`, no `+ leadIn`) and `rehearsalAt` takes
   `startSample` and maps `positionSamples - startSample`: the first bar is the
   lead-in, the song starts at `startSample + leadInSamples`, and a later-section
   run counts in on, and plays from, the section it was launched on. The Song
   stores the start sample in session state (`sessionStorage`, restored on
   remount because the tab unmounts) and passes it through. The backend contract
   is unchanged (it starts at the sample it is given) and the absolute end
   `leadIn + totalBars * barSamples` is the same for any start section.
   `Tests/check-song-model.mjs` gained the corrected start-sample expectations
   and a later-section run (count-in at 2000, playing at 3000 = section 1 bar 0,
   done at 7000).
2. **Take-melody audition could start over a rehearsal.**
   `setMelodyPlayback(..., true)` now clears `practiceRunning` (alongside
   `setSnippetMelodyPlayback(..., true)`), so one transport at a time.
3. **The end stop only existed while the Song tab was mounted.** Added an
   App-level safety effect (App has `songs`/`activeSongId`/`transport`): while
   `practiceRunning`, `end = lead + totalBars * barSamples` and a one-shot ref
   emits the stop once `practicePosition >= end`. The Song's local stop stays;
   the backend's idempotent disable makes the duplicate harmless.

Docs updated (ADR-0009, AGENTS.md, this ticket) to the corrected start/lead-in
wording. Re-verified below; still no commit and still `in-progress` pending the
real-WebView2 smoke test.

- 2026-11-21: Linux CI caught a juce::var -> int64_t ambiguity in the
  rontendSetPracticeRunning handler (int64_t is long on Linux while the
  var conversion is juce::int64). The cast now uses juce::int64;
  Windows/macOS were unaffected. Local Release build and CTest 5/5 green.
