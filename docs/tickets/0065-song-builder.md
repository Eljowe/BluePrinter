---
id: "0065"
title: "Song builder: plan sections, chords and recordings"
status: in-progress
blocked_by: []
---

# Song builder: plan sections, chords and recordings

## Problem

BluePrinter captures takes and loops and keeps them in a library, but there is no
place to plan a song: which sections exist, how many bars each has, the chords,
and which recording belongs where. The practice tools (chords, scales, melody
extraction) and the recordings are not connected, so a demo has to be assembled
in the user's head or in another app.

## Current behaviour

- The Record section has two tabs, Take and Loop (`recordingMode`, persisted as
  `bp:recordingMode`).
- Takes and loops save to the library as snippets; the library plays one snippet
  at a time. Setlists are flat ordered lists of snippet ids stored in the
  properties file (`SetlistStore`, key `setlists`).
- Chord theory is pure JS in `WebUI/src/chords.js` (`buildChordCatalog`,
  `rankChords`); `SCALES` lives in `WebUI/src/utils.js`.
- The looper's Length selector offers 0/1/2/4/8 bars, while
  `Looper::setLengthBars` already clamps 0..16 and
  `LooperGrid::computeFixedLengthSamples` supports any bar count.
- Snippets carry no BPM or bar count.

## Design decisions

1. A third tab, **Song**, beside Take and Loop. It is a planner, not a timeline:
   no audio regions, no mixer, no gapless sequencing.
2. A song is a document: name, key (root + major/minor), BPM, meter, ordered
   sections. A section: name, bars, chord events, melody/idea text, one linked
   snippet, recorded BPM/bars.
3. **One linked recording per section.** The looper can overdub a full bed into
   one loop and the library keeps alternates; the song links one.
4. Section bars are loopable counts: 1, 2, 4, 8, 12, 16. A section always maps to
   one fixed-length loop.
5. The song owns BPM/meter. Selecting a song applies them to the transport; while
   the Song tab is active, a header BPM/meter change writes back to the song
   (debounced). The key is the song's own (there is no global key). Changing the
   key does not transpose existing chords; the Suggest templates use the new key.
6. Songs persist in the properties file under `songs` (the `SetlistStore`
   pattern) and are pruned when a linked snippet is deleted.
7. Recording a section uses the existing capture paths: a fixed-length loop
   (length = section bars) or a take. **Save & attach** on the section saves the
   current loop/take and links the new snippet; **Link** picks an existing
   snippet.
8. No new audio path, no new thread, no state-format break.

## Data model

One song, serialised inside the `songs` JSON array:

```json
{
  "version": 1,
  "id": "k3j9x2",
  "name": "Neon Sunday",
  "keyRoot": 9,
  "keyScale": "minor",
  "bpm": 120,
  "numerator": 4,
  "denominator": 4,
  "sections": [
    {
      "id": "s1",
      "name": "Intro",
      "bars": 4,
      "chords": [{ "bar": 0, "symbol": "Am", "name": "A minor" }],
      "idea": "",
      "snippetId": 0,
      "recordedBpm": 0,
      "recordedBars": 0
    }
  ]
}
```

Limits and sanitising (enforced in `SongStore`, mirrored defensively in
`songModel.js`):

- At most 64 songs, 64 sections per song, 512 total bars per song.
- Song name 1..80 chars (trimmed), section name 1..40, idea 0..200, chord symbol
  1..12, chord name 0..40.
- Section bars are normalised to the nearest allowed value in {1,2,4,8,12,16}.
- At most 32 chord events per section, unique bars, sorted by bar, every bar
  within `[0, bars)`. A chord holds from its bar until the next event.
- `bpm` 20..300; `numerator` 1..16; `denominator` in {1,2,4,8,16}; `keyRoot`
  0..11; `keyScale` `major` or `minor`.
- `snippetId` 0 or a positive int; 0 means no link. `recordedBpm` 0 or 20..300;
  `recordedBars` 0..16.
- Ids are 1..32 chars of `[A-Za-z0-9_-]`. Empty/duplicate section ids get a
  deterministic fallback; an empty song id gets a numeric fallback.
- `upsert` sanitises every field and replaces a song with the same id.
  `fromJson` runs the same sanitiser. `prune(validSnippetIds)` clears links to
  missing snippets and returns whether anything changed.

## Required change

### Backend: `Source/SongStore.h/.cpp`

Mirror `SetlistStore` (same shape, same test style):

```cpp
struct SongChord   { int bar = 0; juce::String symbol, name; };
struct SongSection { juce::String id, name; int bars = 8;
                     std::vector<SongChord> chords;
                     juce::String idea; int snippetId = 0;
                     float recordedBpm = 0.0f; int recordedBars = 0; };
struct Song        { juce::String id, name; int keyRoot = 0;
                     juce::String keyScale = "major"; float bpm = 120.0f;
                     int numerator = 4, denominator = 4;
                     std::vector<SongSection> sections; };

class SongStore
{
public:
    void clear();
    const std::vector<Song>& all() const;
    const Song* find (const juce::String& id) const;
    bool upsert (const Song& song);          // sanitise; false if the name is blank
    bool remove (const juce::String& id);
    bool prune (const std::vector<int>& validSnippetIds);
    juce::String toJson() const;
    void fromJson (const juce::String& json);
};
```

`toJson` writes the array with `version: 1` per song. `fromJson` accepts the
array shape, ignores unknown fields, and sanitises each song.

### Backend: processor, editor and bridge

- `PluginProcessor`:
  - `SongStore songs;` plus `armSongPersist()` / `flushSongPersist()` with the
    same 500 ms debounce as setlists. Flush in the existing timer block next to
    `flushSetlistPersist`, and wherever the setlist persist is flushed on
    destruction.
  - `std::vector<Song> getSongs() const`, `bool saveSong(const Song&)`,
    `bool deleteSong(const juce::String& id)`, `void pruneSongsAgainstLibrary()`.
  - `saveSong`/`deleteSong` arm the persist and fire `libraryChanged`.
  - `restoreUserState`: read the `songs` properties key (after the setlists
    block), `fromJson`, then prune against the loaded library.
  - Call `pruneSongsAgainstLibrary()` wherever `pruneSetlistsAgainstLibrary()` is
    called (snippet delete paths and the library reload); coalesce the
    `libraryChanged` notification with the setlist one where they sit together.
  - Extend `createSetlist(const juce::String& name, const std::vector<int>& ids = {})`
    to append the ordered ids (skipping <= 0 and duplicates) in the same call.
    The existing `{ name }` event behaviour is unchanged.
- `WebViewEditor`:
  - New events `frontendSaveSong` (`{ song }`) and `frontendDeleteSong`
    (`{ id }`) in `WebViewEditor.h`; handlers parse the song var into a `Song`,
    call the processor, and let the processor fire `libraryChanged`.
  - `makeSongsSnapshot()` and add `songs` to `backendSnippets` and to
    `withInitialisationData` (next to `setlists`).
  - The `frontendCreateSetlist` handler reads an optional `ids` array and passes
    it to `createSetlist`.
- `WebUI/src/bridge.js`: add `saveSong` / `deleteSong` to `FRONTEND_EVENTS` and
  extend the `createSetlist` comment for `{ name, ids? }`.

### Frontend: `WebUI/src/songModel.js` (pure ESM, no DOM)

- `SECTION_BARS = [1,2,4,8,12,16]`, `SECTION_PRESETS` (name + default bars),
  `SONG_TEMPLATES` (`pop`, `blues`, `blank`).
- `createSong({ name, keyRoot, keyScale, bpm, numerator, denominator, template })`
  returns a fully populated song. `pop` = Intro 4, Verse 8, Chorus 8, Verse 8,
  Chorus 8, Outro 4 with suggested chords; `blues` = Intro 4, 12-bar x2, Outro 4
  with the 12-bar pattern; `blank` = one 8-bar section.
- `normaliseSong(raw)` - defensive parse for the payload (same limits).
- `totalBars(song)`, `songDurationSeconds(song)`, `sectionStartBar(song, index)`,
  `barSamples(bpm, meter, sampleRate)`.
- `addSection`, `removeSection`, `moveSection`, `updateSection`,
  `setSectionBars` (drops chords past the new end), `setChordAtBar`,
  `clearChordAtBar`, `normaliseChords`.
- `suggestChords(song, sectionId, templateId)` - uses `buildChordCatalog` and
  scale degrees. Templates: major `I-V-vi-IV`, `I-IV-V`, `vi-IV-I-V`; minor
  `i-VI-III-VII`, `i-iv-v`; plus `12-bar` (one chord per bar). Chords are placed
  evenly across the section (bar 0, then every `bars / count` bars), and the
  12-bar template uses its fixed bar positions.
- `chartText(song, snippetsById)` - the copyable markdown-ish chart.
- `snippetIdsInOrder(song)` - linked ids in section order, duplicates removed.
- `sectionTone(name)` - a small tone key for the arrangement blocks.
- `newSongId()` - a `localStorage`-safe unique id (timestamp + random base36).

### Frontend: `WebUI/src/components/Song.jsx`

- **Song header**: song `<select>` (all songs) + New (template popover) + inline
  name input + key root/scale selects + `N bars - approx M:SS` + Copy chart.
  When no songs exist, an empty-state card with the three templates.
- **Arrangement strip**: one button per section, `flexGrow = bars`, labelled with
  name and bar count, tone-coloured, selected = black border; clicking selects.
- **Section list** (left): ordered rows with name, bars, chord summary and a
  LOOP/TAKE badge when linked; a selected row is highlighted. Add section button.
- **Section editor** (right): name input + preset chips, bars `<select>` (1, 2, 4,
  8, 12, 16), chord cells (rows of 8, one per bar; a chord holds and later bars
  read as a dot), a chord picker popover (diatonic + borrowed from
  `buildChordCatalog`, roman + symbol, Clear), a Suggest menu, the melody/idea
  input, the linked-recording card (name, play/stop, melody count + Play melody
  when the snippet has notes, missing-link state), and the actions: Record loop,
  Record take, Link, Save & attach (when a loop/take exists), Discard take (when
  a take is pending), Clear link.
- **Footer**: Create setlist (linked ids in order; disabled when none), Delete
  song (two-step inline confirm).
- **Link picker**: a small popover with a search field and the library snippets
  (name, key, duration); selecting links and sets `recordedBpm` to 0 (unknown) and derives `recordedBars` from the snippet's duration at the song tempo.
- **Recording**:
  - `Record loop`: refuse during a capture; emit `setBpm` and
    `setTimeSignature` only when they differ from the song; emit
    `setLooperLengthBars { bars: section.bars }`; switch to the Loop tab; emit
    `setLooperRecording { enabled: true }`; remember the section id so the Song
    tab can show a small badge while the capture is unsaved.
  - `Record take`: same tempo/meter apply; switch to the Take tab; emit
    `startRecording`; remember the section id.
  - `Save & attach`: set a pending `{ songId, sectionId }`, emit `saveLoop` or
    `saveTake { id: transport.selectedTakeId }`. On the next snippets payload,
    diff ids against the previous set, attach the highest new id, set
    `recordedBpm = song.bpm` and `recordedBars` from the snippet's `durationSeconds` at the song's bar length (rounded; fallback `section.bars`), save the song,
    and (when the snippet name starts with `Snippet ` or `Loop `) rename it to
    `<song name> - <section name>` via `frontendUpdateSnippetMeta` preserving
    comments. Clear the pending attach on success, on discard, or after a 2 s
    timeout with no new id.
  - `Discard take` clears the pending take and the section badge.
  - The section badge on the Song tab is derived from the remembered section and
    the current capture state; it clears on attach/discard.
- **Playback**: the linked card's play button uses `startPlayback { id }` /
  `stopPlayback`; the melody button uses `setSnippetMelodyPlayback { id, enabled }`.
  Read the playing state from `transport.playingSnippetId` and
  `transport.melodyPlayingSource`/`melodyPlayingId`.
- **Tempo sync**: when the active song changes, apply its bpm/meter to the
  transport (once per song id). While `recordingMode === "song"`, a header
  bpm/meter change updates the active song (debounced 400 ms) and persists.
- **Persistence**: all edits are built from the current song prop and emitted
  through `frontendSaveSong` (name edits use a local draft + debounce). The
  backend echo through `backendSnippets` reconciles.
- **Accessibility**: tabs and rows are real buttons/roles, chord cells and
  arrangement blocks have labels, selects are labelled, the chord picker is
  Escape-closable, focus-visible comes from the global ring, reorder is
  buttons (not drag-only).

### App wiring

- `readInitialSongs()` + `songs` state; the snippets subscriber reads
  `payload.songs`; `withInitialisationData` already carries it.
- `recordingMode` accepts `"song"`; `readInitialRecordingMode` validates all
  three; the tab bar gets a third tab (`recording-tab-song`, panel
  `recording-panel-song`); `handleRecordingTabsKeyDown` wraps across three.
- `activeSongId` state, persisted as `bp:activeSongId`, validated against the
  payload (fallback: first song, else null).
- Pass `songs`, `snippets`, `transport`, `activeSongId`, `onActiveSongChange`
  and `onSwitchTab` to `<Song>`.
- Global shortcuts: Space in the Song tab records the selected section (the
  selection lives in `Song`, so let `Song` own that keydown with the same
  editable/control guards and make the App handler ignore Space in song mode).
  Enter/Delete stay take-only. Esc stops playback. Update the header Keyboard
  popover.
- Extend the looper Length select with 12 and 16 bars (the backend already
  accepts them).
- `styles.css`: a clearly marked `/* Song builder (0065) */` block using the
  existing tokens; two-column layout (`minmax(0, 1.15fr) minmax(0, .85fr)`),
  collapsing to one column under ~560px; no gradients or new colours.

### Tests

- `Tests/test_SongStore.cpp` (add to `BluePrinterTests` in `CMakeLists.txt`):
  upsert/delete, JSON round-trip, sanitising (blank name rejected, bars
  normalised, chords sorted/deduped/out-of-range dropped, limits), prune,
  duplicate ids, version tolerance.
- `Tests/check-song-model.mjs` (new CTest `SongModel`, add next to
  `ChordTheory`): templates and total bars, duration/bar math, add/remove/move,
  setSectionBars dropping chords, chord normalisation, suggestChords for a major
  and a minor key plus 12-bar, chart text, snippet order, `normaliseSong`
  rejecting bad input.
- Keep the existing tests green.

### Docs

- `docs/adr/0008-song-builder-is-a-plan.md`: the plan-not-timeline decision, one
  link per section, capture attach, persistence, tempo sync, and why there is no
  sequencing/mixing.
- `AGENTS.md`: Record section (third tab, song document, record/attach flow,
  events, persistence, shortcuts), the `songs` payload, the looper 12/16 length
  options, and the new files.
- `CONTEXT.md`: terms **Song**, **Section**, **Chord event**, **Capture
  attach**; module map entries; invariants (no audio path change, one link per
  section).
- `.opencode/skills/webui-bridge/SKILL.md`: the two new events and the
  `createSetlist` ids field.
- Ticket status `in-progress` with an implementation comment; README row and
  notes. Do **not** mark done without the manual real-WebView2 smoke test.

## Acceptance criteria

- A user can create a song from a template, see the total bars/duration, edit
  the key/tempo/meter, add/remove/reorder sections, set bars, set one chord per
  bar (with a chord picker and suggestions), and write a melody/idea line.
- "Record loop" applies the song tempo/meter, sets the looper length to the
  section bars, starts the count-in/capture, and the resulting loop can be saved
  and attached to that section in one action; "Record take" and "Link" work too.
- The linked recording plays from the section card; a snippet melody can be
  auditioned when present; a missing link is shown and clearable.
- Copy chart puts the full chart on the clipboard; Create setlist produces an
  ordered setlist of the linked recordings; Delete song removes it.
- Songs and links survive a restart (with a library folder set), and a deleted
  snippet clears its link. No library folder shows a hint that links are
  session-only.
- The active song and the tab survive a restart.
- Existing Take/Loop behaviour, bridge parity, tests, and the audio path are
  unchanged.

## Files

New: `Source/SongStore.h/.cpp`, `Tests/test_SongStore.cpp`,
`WebUI/src/songModel.js`, `WebUI/src/components/Song.jsx`,
`Tests/check-song-model.mjs`, `docs/adr/0008-song-builder-is-a-plan.md`.

Touched: `Source/PluginProcessor.h/.cpp`, `Source/WebViewEditor.h/.cpp`,
`WebUI/src/App.jsx`, `WebUI/src/bridge.js`, `WebUI/src/styles.css`,
`WebUI/src/components/Looper.jsx`, `CMakeLists.txt`, `AGENTS.md`,
`CONTEXT.md`, `.opencode/skills/webui-bridge/SKILL.md`,
`docs/tickets/0065-song-builder.md`, `docs/tickets/README.md`.

## Out of scope

- Rehearsal/click-only transport, live section highlight (v2).
- Multiple linked recordings per section, comping, a timeline, a mixer,
  gapless song playback, MIDI/file export.
- Tempo/bars metadata on snippets, time-stretch, backing-track import.
- Drag-only reorder, a melody editor/roll in the Song tab.

## Comments

- 2026-11-21: Design agreed with the user. Scope: plan + record (sections,
  chords, melody notes, templates, per-section loop/take record and attach,
  link existing, chart, setlist). Rehearsal is a fast follow, not v1.
- 2026-11-21: Implementation landed on `feature/0065-song-builder` (left
  uncommitted for review). Backend: `Source/SongStore.h/.cpp` (limits,
  sanitiser, JSON, prune) with `Tests/test_SongStore.cpp`; `PluginProcessor`
  owns the `songs` store, the 500 ms debounced persist, the prune call sites
  and the restore load; `WebViewEditor` ships `songs` in `backendSnippets` and
  the initial blob, adds `frontendSaveSong { song }` / `frontendDeleteSong
  { id }`, and `frontendCreateSetlist` accepts ordered `ids`. Frontend:
  `WebUI/src/songModel.js` (templates, bar math, editing helpers,
  `suggestChords` via `buildChordCatalog`, `chartText`) with
  `Tests/check-song-model.mjs` (CTest `SongModel`), `Song.jsx`, the third tab
  and App wiring (`bp:activeSongId`, `bp:recordingMode` incl. `song`, tempo
  sync both ways, Space owned by `Song`, header Keyboard popover), and the
  looper Length select now offers 12/16 bars. Docs: ADR-0008,
  AGENTS/CONTEXT/webui-bridge skill. Verification: `BluePrinterTests`
  237/237, CTest 5/5 (`BluePrinterTests`, `BridgeEventParity`, `ScaleTheory`,
  `ChordTheory`, `SongModel`), WebUI build OK, bridge parity 115 frontend /
  7 backend. Still `in-progress`: the manual real-WebView2 smoke test must
  cover the three templates; name/key/tempo/meter edits (both directions);
  add/remove/reorder sections; bars + chord picker/Suggest/Clear; idea;
  Record loop with count-in then Save & attach (rename + comment preservation,
  `recordedBars`); Record take then Save & attach; Link/Clear/missing link;
  play + melody audition; Copy chart; Create setlist order; two-step Delete
  song; restart persistence (active song, tab, links); deleting a linked
  snippet clears the link; the no-library-folder hint.
- 2026-11-21: Review pass (no commit). Fixed: `clearChordAtBar` was used
  without an import (the "Clear bar" button threw); the tempo write-back now
  arms when a selected song already matches the transport (`song?.id` in the
  write-back deps + an explicit arm in the apply effect); the song-name
  debounce is flushed on unmount, Enter, a song switch and New (still not on
  blur, so a blur+click can't clobber the click); a preset updates the
  section-name draft; the linked card shows `recordedBpm` with a
  `--warn-ink` mismatch warning against the song tempo; a section capture
  clears the loop/take Dub toggle first so it is always a fresh capture; the
  Song tab badge and "Recording…" hint clear when the take/loop is discarded
  from the Take/Loop tab (grace-gated poll, hint gated on `capturing`); the
  attach rename is skipped without a library folder (no error toast); the
  preset list gained Pre-Chorus 4; the key select explains that changing the
  key does not transpose; README spacing fixed; the model test asserts the
  preset list and the minor 12-bar's borrowed harmonic-minor V. Verification
  re-run: `BluePrinterTests` 237/237, CTest 5/5, WebUI build OK, bridge parity
  115 frontend / 7 backend; the manual real-WebView2 smoke test still gates
  `done`.