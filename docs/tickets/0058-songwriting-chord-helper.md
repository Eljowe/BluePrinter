---
id: "0058"
title: "Songwriting helper: chord suggestions from a scale, piano and fretboard note picker"
status: done
blocked_by: []
---

# Songwriting helper: chord suggestions from a scale, piano and fretboard note picker

## Problem

BluePrinter can show a scale on a fretboard and detect the key of a take, but it
cannot help a user move from that scale to a chord idea. A songwriter has to work
out compatible chords in their head or leave the app. There is no lightweight
scratchpad where they can press the notes they hear, see which scale tones fit,
and get chord suggestions to try next.

## Solution

Add a frontend-only **Chords** header tool beside Tuner and Scales. It opens a
wide popover containing:

- Key and scale selectors.
- A one-octave piano and a horizontally scrollable standard-tuning fretboard,
  both used to toggle pitch classes.
- A left-hand suggestion rail listing diatonic and common borrowed chords.
- A current-selection readout with the identified chord name (when recognisable)
  and a Clear action.

Selecting a suggestion loads its chord tones into the instrument views. The
suggestions rank chords by how well they match the current selection, so pressing
notes first and then browsing suggestions works naturally. The feature is visual
only: it makes no sound, changes no audio state, and persists nothing.

## User Stories

1. As a songwriter, I want a **Chords** tool in the header beside Tuner and
   Scales, so that songwriting help lives where my other practice tools live.
2. As a songwriter, I want opening Chords to close Tuner or Scales, so that only
   one tool popover is open at a time.
3. As a songwriter, I want Chords to not start the tuner analysis, so that
   opening a visual theory tool does not change audio analysis state.
4. As a songwriter, I want to choose one of the twelve key roots, so that the
   suggestions use the musical key I am writing in.
5. As a songwriter, I want to choose major, natural minor, major pentatonic,
   minor pentatonic or blues, so that the scale highlighting matches the sound I
   am exploring.
6. As a songwriter, I want the scale tones highlighted on both the piano and the
   fretboard, so that I can see the compatible notes without translating theory.
7. As a songwriter, I want the scale root visually distinct, so that I can find
   the tonic quickly.
8. As a songwriter, I want a one-octave piano view, so that I can click notes in
   a familiar layout.
9. As a songwriter, I want a standard-tuning guitar fretboard, so that I can
   find the same notes on my instrument.
10. As a songwriter, I want the fretboard to scroll horizontally when it is wider
    than the popover, so that the layout remains usable at the fixed app width.
11. As a songwriter, I want clicking a piano key to add or remove that pitch
    class, so that I can build a chord or note set quickly.
12. As a songwriter, I want clicking a fretboard position to add or remove that
    pitch class, so that I can work from the guitar layout.
13. As a songwriter, I want the piano and fretboard selections to stay in sync,
    so that changing a note in one view updates the other.
14. As a songwriter, I want the same pitch class selected on every occurrence
    across the fretboard, so that the selection reflects the note, not a
    particular octave or string.
15. As a songwriter, I want to toggle a selected note off, so that I can correct
    a chord without clearing everything.
16. As a songwriter, I want a Clear action, so that I can start a new idea
    immediately.
17. As a songwriter, I want a readout of the selected notes, so that I always
    know what I have chosen.
18. As a songwriter, I want the readout to name the chord when the selection
    matches a common chord, so that I can learn what I just played.
19. As a songwriter, I want the readout to fall back to note names when the
    selection does not match a common chord, so that unusual voicings remain
    understandable.
20. As a songwriter, I want the suggestion rail populated before I select
    anything, so that I can browse the key's palette for inspiration.
21. As a songwriter, I want suggestions ranked after I select notes, so that the
    most compatible chords appear first.
22. As a songwriter, I want suggestions that contain all selected notes ranked
    ahead of suggestions that merely share some tones, so that the rail feels
    responsive to my selection.
23. As a songwriter, I want the currently identified chord excluded from the
    suggestion list, so that the rail shows next options rather than repeating
    what I already selected.
24. As a songwriter, I want in-key chords and borrowed chords separated into
    groups, so that I understand which suggestions are diatonic and which are
    colour choices.
25. As a songwriter, I want each suggestion to show its Roman numeral, chord
    name and constituent notes, so that I can choose by theory or by sound.
26. As a songwriter, I want a triads/sevenths toggle, so that I can move between
    simple triads and richer seventh chords.
27. As a songwriter, I want clicking a suggestion to replace the current
    selection with that chord's notes, so that I can audition it visually with
    one click.
28. As a songwriter, I want changing the key or scale to keep my selected notes
    and recompute suggestions, so that I can explore the same shape in another
    harmonic context.
29. As a songwriter, I want the key, scale and selection to remain while I work
    during the session, so that closing and reopening the popover does not lose
    the idea.
30. As a songwriter, I want nothing written to `localStorage` or host state, so
    that starting the application always starts a clean scratchpad.
31. As a keyboard user, I want to focus the popover on open, navigate the
    controls and note buttons with Tab, and close with Escape, so that the tool
    is fully keyboard-operable.
32. As a keyboard user, I want suggestions and note cells to expose accessible
    names and pressed states, so that selection is not conveyed by colour alone.
33. As a user, I want an outside pointer press to close the popover, so that it
    behaves like the Tuner and Scales tools.
34. As a user, I want focus returned to the Chords button on close, so that I do
    not lose my place in the header.
35. As a user, I want the tool to make no sound and change no capture, monitor,
    chain, looper or playback state, so that songwriting help never interferes
    with recording.

## Implementation Decisions

1. **Frontend-only.** The feature adds no C++ module, no bridge event and no
   transport snapshot field. Bridge event parity must remain unchanged.
2. **Header integration.** Chords becomes a third header tool beside Tuner and
   Scales. The existing Keyboard item is the shortcut help and remains
   unchanged.
3. **Tool ownership.** Chords participates in the existing single-owner header
   tool state but is explicitly a non-analysis tool. Opening it closes Tuner or
   Scales and stops their shared tuner analysis. Opening Tuner or Scales closes
   Chords. The tool-owner reconcile logic must preserve Chords when the backend
   `tunerOpen` flag is false, rather than clearing every non-null tool.
4. **Presentation.** A right-aligned popover, wide enough for a suggestion rail
   plus instrument area, with a maximum height and its own vertical scrolling.
   The guitar fretboard has its own horizontal scroll container so a narrow
   viewport does not force the whole popover wider.
5. **Theory module.** All chord and suggestion logic lives in a new pure
   frontend module. It reuses the existing scale helpers and standard tuning
   data from `utils.js`; it does not duplicate scale spelling logic.
6. **Scale and chord relationship.** The selected key and scale control the
   highlighted compatible notes on both instruments. Chord generation uses the
   selected key's parent seven-note mode: major for major and major pentatonic;
   natural minor for natural minor, minor pentatonic and blues. The suggestion
   rail labels the chord context as the parent major or minor key so the
   distinction is explicit.
7. **Diatonic catalogue.** For a major parent, the triads are I, ii, iii, IV, V,
   vi, vii°. With sevenths enabled they are Imaj7, ii7, iii7, IVmaj7, V7, vi7,
   viiø7. For a natural-minor parent, the triads are i, ii°, III, iv, v, VI,
   VII. With sevenths enabled they are i7, iiø7, IIImaj7, iv7, v7, VImaj7, VII7.
8. **Borrowed catalogue.** Major keys add four common borrowed options: iv
   (parallel minor), bVI (parallel minor), bVII (mixolydian) and II as V/V
   (secondary dominant). Minor keys add IV (dorian/parallel major), V (harmonic
   minor), I with a Picardy third and bII (Neapolitan). Borrowed sevenths use
   the conventional quality for that chord (for example, II becomes a dominant
   seventh and iv a minor seventh).
9. **Selection model.** Selection is a set of pitch classes, independent of
   octave, string or inversion. Clicking a note toggles it; clicking a
   suggestion replaces the selection with that chord's pitch classes; Clear
   empties it. Changing key or scale keeps the selected pitch classes.
10. **Suggestion ranking.** With no selection, suggestions use catalogue order
    (diatonic first, then borrowed). With a selection, chords that contain every
    selected pitch class rank above partial matches; ties are resolved by
    shared-tone count, then by whether the chord root is selected, then
    diatonic-before-borrowed, then stable catalogue order. The currently
    identified chord is omitted from the suggestion list.
11. **Chord identification.** A pure recogniser maps selected pitch classes to
    common chord names when the set matches a known template. It supports at
    minimum major, minor, diminished, augmented, sus2, sus4, major seventh,
    minor seventh, dominant seventh, half-diminished seventh and diminished
    seventh. Ambiguous sets resolve deterministically; unrecognised sets return
    no chord name and the UI shows the note list instead.
12. **Instrument views.** The piano is one octave of pitch classes rendered as
    real white and black keys, using buttons with accessible names. The
    fretboard uses standard tuning, low-E to high-E, with 0–24 frets and a
    horizontal scroll container. Visible previous/next scroll buttons (disabled
    at the ends), arrow-key scrolling on the focused container, and Home/End
    make the extra frets reachable. Every note cell is a button labelled with
    its note name, string and fret. Scale tones, root, selected notes and
    out-of-scale notes are distinguishable by more than colour alone.
13. **State.** The selected key, scale, triad/seventh mode and selected pitch
    classes live in session-only React state owned above the mounted popover, so
    closing and reopening the tool retains the current idea. Nothing is written
    to `localStorage`, the properties file or host plugin state.
14. **Accessibility.** The popover is a labelled dialog, focused on open, closes
    on Escape, returns focus to its header button, and exposes pressed states on
    note and suggestion buttons. The selection readout is announced politely.
15. **No audio.** The tool must not audition notes, route through chains, affect
    the monitor mix, capture, looper, playback or metronome, or add a synth.

## Testing Decisions

The single new seam is the pure chord-theory module. UI wiring stays
presentational and is verified by the WebUI build plus a manual WebView2 pass.

Good tests exercise behaviour through the module's public functions:

- Build the diatonic catalogue for C major and assert the exact triads,
  qualities, Roman numerals and pitch classes for all seven degrees.
- Build the seventh catalogue for C major and assert Imaj7 through viiø7.
- Build the natural-minor catalogue for A minor and assert its triads and
  sevenths.
- Assert borrowed chords for C major and A minor include the intended pitch
  classes and qualities, including the V/V dominant seventh in major and the
  harmonic-minor V in minor.
- Assert the parent-mode mapping for major pentatonic, minor pentatonic and
  blues.
- Run all twelve roots through both parent modes and assert seven diatonic
  entries with the expected set sizes and no missing or duplicate pitch
  classes.
- Assert key-aware spelling for sharp and flat keys using the existing scale
  spelling helpers.
- Assert ranking behaviour: empty selection returns catalogue order; an exact
  chord is excluded from suggestions; full-match chords rank above partial
  matches; ties are deterministic; selecting the chord root gives the intended
  tie-break.
- Assert chord identification for representative sets (C major, A minor, C7,
  Am7, B diminished, B half-diminished, sus chords) and an unrecognised set.
- Extend the existing tool-owner tests so a non-analysis Chords tool is
  preserved when `tunerOpen` is false and still yields to a late host restore
  that opens the analysis.
- Confirm the WebUI production build and bridge parity check stay green, and
  manually verify the popover, scrolling, keyboard operation and focus return
  in a real WebView2 session.

Prior art: `Tests/check-scale-theory.mjs` wired into CTest as `ScaleTheory`, and
`Tests/check-bridge-events.mjs` for the parity check. Add a sibling Node test
script rather than introducing a React test framework.

## Out of Scope

- Live audio pitch capture or chord detection from recorded audio.
- MIDI keyboard input or MIDI note capture.
- Auditioning chords through a synth or the VST3 chains.
- Persisting anything to `localStorage`, the properties file or host state.
- Saving named progressions, ideas or setlists.
- A bar-based progression builder, rhythm editor or arrangement view.
- Voice leading, inversions, slash chords, drop voicings or custom chord
  spelling.
- Alternate tunings, capo, left-handed mode, 7/8-string layouts or bass
  fretboards.
- Extended chords beyond sevenths (ninths, elevenths, thirteenths, altered
  dominants).
- MIDI file export (ticket 0026 stays `wontfix`).
- Replacing or changing the Keyboard shortcut help item.

## Docs

- `webui-bridge` skill: header tool/popover pattern, component conventions and
  accessibility requirements.
- `AGENTS.md` header-tools note once implemented.
- `CONTEXT.md`: add the chord-scratchpad term if the module becomes a
  load-bearing part of the UI vocabulary.

## Files

WebUI-first:

- New pure theory module under `WebUI/src/` for chord catalogues, ranking and
  chord identification.
- New presentational component under `WebUI/src/components/`.
- `WebUI/src/App.jsx` for header-tool ownership and session state.
- `WebUI/src/utils.js` only if a small shared helper genuinely belongs there;
  existing scale helpers are reused rather than rewritten.
- `WebUI/src/styles.css` for the popover, piano, fretboard, suggestion rail and
  responsive scrolling.
- New Node test script under `Tests/` plus its CTest registration in
  `CMakeLists.txt`.
- Existing scale-theory tests updated for the Chords tool-owner behaviour.

## Results

Implemented as a frontend-only tool. `WebUI/src/chords.js` holds the pure
theory: parent-mode selection (major/minor for the five scales), diatonic
triads/sevenths, the four borrowed chords per mode, deterministic
selected-tone ranking with the exact current chord omitted, common-chord
identification (including sus), and key-aware spelling that prefers the
visible scale's names before the parent/key convention. `Chords.jsx` renders
the key/scale selectors, triads/7ths switch, ranked In key/Borrowed rails, a
one-octave piano, a scrollable 0–24-fret standard-tuning fretboard (visible
previous/next buttons, arrow-key/Home/End scrolling on the focused viewport),
the selected-note/chord readout and Clear. Note cells carry `aria-pressed`, the
in-scale state has a dot shape cue (not colour alone), and the dialog focuses
on open, closes on Escape/outside and returns focus to its trigger. Chords is
a non-analysis tool in the shared header/quick-bar practice cluster: it adds
no bridge event, starts no tuner worker, and closes Tuner/Scales through the
existing `frontendSetTunerOpen` event. Its state is session-only React state
(no `localStorage`/host state).

`Tests/check-chord-theory.mjs` (`ChordTheory` CTest) covers the parent
mapping, exact diatonic/borrowed qualities, Roman numerals and pitch classes
for C major and A minor (triads and sevenths), all 12 roots in both modes,
key/sharp/flat spellings, selected-note spelling for Eb minor and A blues,
ranking/tie behaviour and chord identification. `check-scale-theory.mjs` was
extended for the Chords tool-owner rule. The three Node CTests
(`ChordTheory`, `ScaleTheory`, `BridgeEventParity`), the WebUI production
build and a headless-Chrome interaction smoke test (C major selection, Am
suggestion, Escape close, reopen retains the idea) pass. The full CTest
executable could not be run from this WSL shell because the configured build
cache is a Windows Visual Studio generator; CI will run it. Not attempted in a
real WebView2 session.

### Reflection

Later refinement (same ticket):

- The Chords / Find scales segmented switch is pinned next to the heading (the
  header controls right-align) so it no longer jumps when the mode changes the
  set of key/scale controls beside it.
- The identified chord moved from a small text row into a prominent
  **Detected chord** card with a large symbol, explicit kicker, notes and
  actions, so the "what am I playing" answer is hard to miss.
- A session-only **chord set** collects chords from suggestion rows (`+`),
  the Detected chord card or the chips themselves. `scalesForChords` unions the
  collected chords' pitch classes and lists every scale that contains all of
  them; clicking a scale chip previews it in either mode. `chordSetKey`
  identifies a chord by its pitch classes so the same shape collected from
  different keys de-duplicates correctly. The strip sits above the body and is
  shown in both modes whenever the set is non-empty.
- `Tests/check-chord-theory.mjs` now covers `scalesForChords` (full fits only,
  union semantics, tighter-first ordering) and `chordSetKey`.
