---
id: "0067"
title: "Song sections carry lyrics"
status: in-progress
blocked_by: []
---

# Song sections carry lyrics

## Problem

The Song tab plans sections, chords, a one-line melody/idea and one linked
recording per section, but there is nowhere to write the words. Players either
cram a line into **Melody / idea** (200 chars, single line) or keep lyrics in
another app, so the plan and the words drift apart. Rehearsal shows the chord
but not what to sing.

## Current behaviour

- `SongSection` (`Source/SongStore.h`) holds `id`, `name`, `bars`,
  `chords`, `idea`, `snippetId`, `recordedBpm`, `recordedBars`.
- `SongStore::toJson`/`fromJson` and
  `WebViewEditor.cpp`'s `songToVar`/`songFromVar` carry exactly those fields.
- `WebUI/src/songModel.js` mirrors the limits (`normaliseSong`,
  `addSection`, `createSong`) and `chartText` prints the idea.
- The Song tab (`WebUI/src/components/Song.jsx`) edits the idea with a
  single-line input (`ideaDraft`, commit on blur/Enter) and shows `idea` only
  in the chart.

## Design decisions

1. **Lyrics belong to the section**, not to the song and not to a bar: one free
   multi-line `lyrics` string per section, 0..2000 characters, `\n` line
   breaks. There is no per-bar lyric timing, no karaoke highlight and no
   lyric playback. A song's lyrics are the ordered concatenation of its
   sections' fields.
2. **Persistence is additive**: an optional `lyrics` field next to `idea`
   inside the existing `songs` JSON. No version bump (the loader already
   ignores unknown fields and treats absent fields as defaults), no new
   properties key, no new bridge event. Old documents load with `lyrics: ""`.
3. **Editing is a `<textarea>`** under **Melody / idea**, committed on blur and
   on **Ctrl/Cmd + Enter**; plain **Enter inserts a line break**. `maxLength`
   is 2000. A draft state mirrors the existing `ideaDraft` so a 30 Hz transport
   snapshot cannot fight the caret.
4. The section row meta line shows `· LYRICS` when the section has lyrics, so
   the arrangement is scannable without opening every section.
5. **Rehearsal shows the current section's lyrics** under the big chord
   readout while playing (not during the count-in). The text only changes when
   the section changes; no `aria-live`.
6. `chartText` prints the lyrics under the idea, so **Copy chart** carries the
   words.
7. No audio-path change, no capture/print change, no host-state key, no
   thread/lifetime change. This is message-thread document text only.

## Required change

### Backend

`Source/SongStore.h`:

- Add `juce::String lyrics;` to `SongSection` (after `idea`), and extend the
  header comment's field list.

`Source/SongStore.cpp`:

- `constexpr int maxLyricsChars = 2000;`
- In `sanitiseSong`: `entry.lyrics = trimmedTo (section.lyrics, maxLyricsChars);`
  (`trimmedTo` already trims the ends and keeps internal newlines; do not
  collapse them).
- `toJson`: `sectionObj->setProperty ("lyrics", section.lyrics);` after `idea`.
- `fromJson`: `section.lyrics = readString (*sectionObj, "lyrics");` after
  `idea` (absent -> empty).
- `prune` is untouched (lyrics are never library-dependent).

`Source/WebViewEditor.cpp`:

- `songToVar`: set `lyrics` after `idea`.
- `songFromVar`: read `lyrics` after `idea`.

`WebUI/src/bridge.js` needs no change (lyrics ride `frontendSaveSong { song }`
and the existing `backendSnippets` snapshot).

### Frontend pure model

`WebUI/src/songModel.js`:

- `const MAX_LYRICS = 2000;`
- `normaliseSong`: `lyrics: trimmedTo(rawSection.lyrics, MAX_LYRICS),`
  (between `idea` and `snippetId`).
- `addSection`: `lyrics: section.lyrics ?? "",`.
- `createSong`'s template sections: `lyrics: "",`.
- `chartText`: after the idea line, when `section.lyrics.trim()` is non-empty,
  push `Lyrics:` and then each lyric line verbatim (no prefix). Keep the
  existing blank line per section.
- Update the module header comment to name the lyrics field.

### Song tab

`WebUI/src/components/Song.jsx`:

- `const [lyricsDraft, setLyricsDraft] = useState("");`, reset in the existing
  `[song?.id, sectionId]` effect from `section?.lyrics`.
- `commitLyrics`: trim, slice to 2000, compare to `section.lyrics`, and
  `emitSave(updateSection(song, section.id, { lyrics: trimmed }))` only on a
  change.
- A labelled textarea under the idea field:
  ```jsx
  <label className="song-field song-lyrics">
    <span>Lyrics</span>
    <textarea
      rows={4}
      value={lyricsDraft}
      maxLength={2000}
      placeholder="One line per lyric line"
      onChange={(event) => setLyricsDraft(event.target.value)}
      onBlur={commitLyrics}
      onKeyDown={(event) => {
        if (event.key === "Enter" && (event.ctrlKey || event.metaKey)) {
          event.preventDefault();
          commitLyrics();
        }
      }}
      aria-label="Section lyrics"
    />
  </label>
  ```
  (Keep the exact commit-on-blur behaviour: selecting another section blurs
  first, so the edit is saved before the draft resets.)
- Section row meta: append `· LYRICS` when `entry.lyrics` is non-empty,
  before the `LINKED`/`MISSING` marker.
- Rehearsal readout: when `practiceRunning && rehearsal.phase === "playing"`
  and `practiceSection?.lyrics` is non-empty, render
  `<div className="song-rehearsal-lyrics">{practiceSection.lyrics}</div>`
  after the chord block. Do not render it during `countin` or `done`.
- Empty-state copy: mention lyrics beside the melody/idea line.

`WebUI/src/styles.css`:

- `.song-lyrics { width: 100%; }`
- `.song-field textarea` (or `.song-lyrics textarea`): `2px solid var(--line)`
  border, `var(--bg)` background, `var(--ink)` text, 0.78rem, padding 6px 8px,
  `resize: vertical`, `font-family: var(--font)`, and an accent border on
  focus. Square corners, no gradients; reuse the existing tokens.
- `.song-rehearsal-lyrics`: inline-block/full-width wrapped text,
  `white-space: pre-wrap`, `font-size: 0.72rem`, `color: var(--ink-2)`,
  `max-height: 5.5em`, `overflow-y: auto`, `border-left: 2px solid var(--line)`,
  left padding. It must not push the chord readout out of the bar: the
  rehearsal bar already wraps, so keep the block on its own line.

### Tests

`Tests/test_SongStore.cpp`:

- Give `makeSong`'s section a two-line `lyrics`; assert the upsert result and
  the JSON round trip keep it exactly (including the `\n`).
- In the sanitise test, set 2100 chars and assert 2000 with an internal
  newline preserved.
- In the tolerant-JSON test, assert a section object without `lyrics` loads
  with an empty string.

`Tests/check-song-model.mjs`:

- `normaliseSong` keeps a two-line lyric string exactly and caps 2100 chars at
  2000; a section without `lyrics` normalises to `""`.
- `addSection` defaults `lyrics` to `""`; `updateSection(..., { lyrics })`
  sets it and a later unrelated patch keeps it.
- `chartText` prints `Lyrics:` and the lines when present and omits both when
  empty.
- Keep every existing check green.

### Docs

- `docs/architecture/webui.md` (Song builder 0065 section): add `lyrics` to the
  documented section shape, the limits list (0..2000, line breaks kept), and
  one sentence each for the textarea, the row marker, the rehearsal readout
  and the chart output.
- `CONTEXT.md` **Section** entry: add lyrics to the field list.
- Ticket 0067 `in-progress` + an implementation comment; README row
  `in-progress`. Do not mark it `done` (manual WebView2 smoke test outstanding).
- `AGENTS.md` needs no change: it only points at `webui.md` for the song UI.

## Acceptance criteria

- A section has a multi-line **Lyrics** editor; typing, blurring, switching
  sections and restarting the app keep the text (properties file round trip).
- Enter inserts a newline; Ctrl/Cmd + Enter and blur save; a 2000-char cap
  applies in the UI and in `SongStore`.
- Sections with lyrics show `· LYRICS` in the list; **Copy chart** includes
  `Lyrics:` and the lines under the idea.
- Rehearsing a section shows its lyrics under the current chord and updates
  only when the section changes.
- Old songs without `lyrics` load and render normally with empty lyrics.
- No new event, no bridge-parity change, no audio-path change; tests green.

## Files

Touched: `Source/SongStore.h/.cpp`, `Source/WebViewEditor.cpp`,
`WebUI/src/songModel.js`, `WebUI/src/components/Song.jsx`,
`WebUI/src/styles.css`, `Tests/test_SongStore.cpp`,
`Tests/check-song-model.mjs`, `docs/architecture/webui.md`, `CONTEXT.md`,
`docs/tickets/README.md`.

New: `docs/tickets/0067-song-section-lyrics.md`.

## Out of scope

- Per-bar or per-chord lyric timing, karaoke highlighting, lyric scrolling.
- Multiple verses per section, a song-wide lyric sheet view, lyric import or
  paste from a file.
- Any audio, capture, print, rehearsal-transport or persistence-key change.

## Comments

### 2026-11-22 — Implementation (branch `feature/0067-section-lyrics`)

Built as specified. What landed:

- **Backend**: `SongSection::lyrics` (after `idea`), `maxLyricsChars = 2000` and
  `trimmedTo` in `sanitiseSong` (ends trimmed, internal `\n` kept). `toJson` /
  `fromJson` and `WebViewEditor`'s `songToVar` / `songFromVar` carry `lyrics`
  after `idea`; absent loads empty. No version bump, no new event, no new
  properties key, and `prune` is untouched.
- **Model**: `MAX_LYRICS = 2000`; `normaliseSong` (between `idea` and
  `snippetId`), `addSection`, `createSong`'s template sections; `chartText`
  prints `Lyrics:` and the lines verbatim after the idea only when non-empty.
- **Song tab**: `lyricsDraft` reset in the existing `[song?.id, sectionId]`
  effect; `commitLyrics` (trim, slice 2000, emit only on change) on blur and
  Ctrl/Cmd + Enter; a labelled `<textarea rows={4} maxLength={2000}>` under
  **Melody / idea** (plain Enter inserts a newline); `· LYRICS` in the row meta
  before `LINKED`/`MISSING`; the rehearsal bar prints the running section's
  lyrics under the big chord while `phase === "playing"` only. Empty-state copy
  mentions lyrics.
- **Styles**: `.song-field textarea` (no 30px height, padding, `var(--bg)`,
  2px `var(--line)`, 0.78rem, vertical resize, accent border on focus),
  `.song-lyrics` full width, `.song-rehearsal-lyrics` on its own flex line with
  `pre-wrap`, `max-height: 5.5em`, scroll and a left rule.
- **Tests**: C++ `makeSong` carries a two-line lyric (upsert + JSON round trip
  including the newline), the sanitise test caps 2101 chars at 2000 with the
  newline at index 1000, and the tolerant-JSON case asserts an absent `lyrics`
  loads empty. Node covers normalise (two lines kept / 2100 capped at 2000 /
  absent -> `""`), `addSection`/`createSong` defaults, `updateSection` set and
  unrelated-patch retention, and the chart block present/absent.

No commit and still `in-progress`: the manual real-WebView2 smoke test is
outstanding.
