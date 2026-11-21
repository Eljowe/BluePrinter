# Web UI: shell, bridge and practice tools

Moved verbatim from `AGENTS.md` (ticket 0064, 2026-11-21). Rules stay in
[`AGENTS.md`](../../AGENTS.md); this file keeps the mechanics and incident history. The event-naming
rule is in AGENTS.md; this doc holds the component mechanics, bridge payloads and per-tool
behaviour.

## Recording tabs (Take / Loop / Song)

- The recording sections sit behind a frontend-only **Take/Loop/Song tab bar** (`recordingMode` in
  `App.jsx`, persisted to `localStorage`): the Take tab shows the transport + take review, the Loop
  tab the looper, the Song tab the song planner (0065); the Take tab shows a red badge dot while a
  take is pending and the Song tab one while a section capture waits to attach.

## Global keyboard shortcuts (0020)

- Global **keyboard shortcuts** (0020, `App.jsx` `keydown` listener): **Space** starts/stops capture
  for the active tab (take record, loop record, or the selected Song section — the Song tab owns
  that key itself and the App handler ignores it there), **Enter** saves the selected take,
  **Delete**/**Backspace** deletes the selected take (no confirmation, 0037), **T** taps the tempo
  (0049; ignored while a capture is active), **Esc** stops playback (and the song rehearsal),
  **Ctrl/Cmd + +/−** steps the interface zoom and **Ctrl/Cmd + 0** returns to Fit (0060, handled by
  `UiScale.jsx`), and **F11** toggles the standalone window maximise; the handler is ignored while a
  text field is focused and defers to focused controls (so Space/Enter still activate a focused
  button), and a header **Keyboard** popover documents the bindings.

## Header controls and the sync strip

- Header-level global controls: the **BPM knob** (`frontendSetBpm`, shared by the take recorder, the
  looper's beat math and the MIDI clock) with its **Tap** button beside it (`frontendTapTempo`; a
  pure `TapTempo` module averages the last 5 tap intervals, resets after a 2 s gap or when the
  average rounds outside the 40..240 window, and reports a whole BPM — the backend ignores taps
  while a take/loop capture is active), the **Dry knob** (`frontendSetDryLevel`, dB −60..0, scales
  the dry pass-through in the monitor and the capture), the Input/Output knobs
  (`frontendSetParameter` — Input is the record trim, Output the master monitor gain) live in
  `HeaderControls.jsx`, and the looper's **Loop** monitor level (`frontendSetLoopLevel`) and **Dub**
  overdub trim (`frontendSetOverdubLevel`, dB −60..0, applied by `mixOverdubLayer` to each new layer
  before it piles in, default 0 = no-op) live in `Looper.jsx`; a `mixOverdubLayer` result that
  reaches 0 dBFS latches the loop clip indicator (same `frontendResetClip { target: "loop" }` as
  0002); the click + MIDI clock toggles live in the **sync strip** (`SyncControls.jsx`, beside the
  recording tabs): the **Click toggle** (`frontendSetMetronome`, master on/off shared by count-ins,
  takes, loop captures and the free-running clock), the **Click: capture toggle**
  (`frontendSetClickDuringCapture`), the **Click sound popover** (`frontendSetClickParams`, plus the
  **bar-accent step row** → `frontendSetClickAccents`, one toggle per beat of the meter) and the
  **Subdiv** selector (`frontendSetClickSubdivision`, off/8ths/triplets/16ths per beat), the **MIDI
  clock toggle** (`frontendSetMidiClock`) + the **Clock: on record toggle**
  (`frontendSetMidiClockOnRecord`) + device picker (`frontendSetMidiDevice`).

## Bridge events and payloads

- **Events**: chain lifecycle via `frontendAddChain`, `frontendRemoveChain`, `frontendRenameChain`,
  `frontendSetChainInputs`, `frontendSetChainRecord`, `frontendSetChainVolume`,
  `frontendSetChainMute`, `frontendSetChainMidiChannels`; slot ops (`frontendAddVst3` etc.) carry a
  `chain` field with the chain id.
- Take review: `frontendSetTakePlayback`, `frontendSaveTake { id }`, `frontendDiscardTake { id }`,
  `frontendSelectTake { id }`, `frontendDiscardAllTakes`; looper: `frontendSetLoopCrop` takes `{ startBeats, endBeats }`
  (whole beats, 4 per bar; the loop waveform's draggable crop handles and wheel zoom/shift+wheel pan
  in `Looper.jsx` drive the same event — a `zoom`-scaled, `pan`-shifted `.looper-track` with
  keyboard-operable `role="slider"` handles), `frontendSetLooperOverdub` (`{ enabled }` — with a
  loop captured and looping on, record **layers over** the loop instead of replacing it: the capture
  writes a layer region after the FULL loop on the audio thread (`overdubWritePos` =
  `audioLoopFullLength`, so a start-cropped window can't be overwritten by the layer;
  `audioLoopLength` stays fixed so the wrap boundary never moves), starts the loop from position 0
  exactly when the layer capture begins (with a count-in the loop stays silent through it, so the
  count-in counts into the loop downbeat and the layer aligns), and `mixOverdubLayer` wrap-mixes the
  layer into the loop on stop under `recordLock`; fresh captures still wipe), `frontendSaveLoop`
  writes straight to the library folder (no dialog, like the take); library:
  `frontendSetSnippetColor`, `frontendRenameTag` (`{ color, name }` — user names for the colour
  tags, persisted as `tagNames` JSON in the properties file and shipped back with every snippets
  payload; empty name resets to the built-in label).
- `backendVst3Chain` ships `inputChannels` + `chains: [{id, name, inputs, wantsMidi, midiChannels, recordOnCapture, volume, muted, pending, slots}]`
  + `openEditors` + `restoring`; `backendTransport` ships `chainLevels: [{chain, level, peak}]` at
  30 Hz.

## Chain rail layout

- Chain panels render in a horizontal rail (fixed-width cards, internal plugin-list scroll) instead
  of a stretching grid.

## Snippet library: gain, seeking, favourites, export

- **Snippets**: `Snippet.gainDb` (dB, −24..+24, default 0) is a **non-destructive playback trim** —
  `renderPlayback` applies `decibelsToGain(gainDb)` to the copied audio (before the master Output)
  and it is never baked into the saved WAV. Persisted as `gainDb` in the JSON sidecar
  (`SnippetLibrary.cpp` read/write + `updateGain`); `frontendSetSnippetGain { id, gainDb }` and
  `frontendNormalizeSnippet { id }` (peak → −1 dBFS) drive it. The gain write and the library
  snapshot are debounced 500 ms (`snippetGainPersistPending` → `flushSnippetGainPersist`) because
  the `SnippetCard` Gain knob emits per pointer move. `snippetToVar` ships `gainDb`;
  `SnippetCard.jsx` expanded details show the Gain knob + a Normalize button.
- **Seeking** (0057): the expanded waveform is a `role="slider"` — click starts playback from that
  point, drag scrubs, arrows/PageUp-Down/Home/End seek. `startPlayback { id, startSample? }` starts
  partway in and `frontendSetPlaybackPosition { position }` jumps the playing snippet;
  `PluginProcessor::setPlaybackPosition` queues an atomic pending seek that `renderPlayback` applies
  after its snippet re-fetch, so the fetch's reset to 0 can't clobber it.
- `Snippet.favourite` (bool, default false) is the **user star** (0035) — persisted as `favourite`
  in the JSON sidecar (`updateFavourite`) and shipped in the snapshot; `frontendSetSnippetFavourite { id, favourite }`
  toggles it (immediate persist + `libraryChanged`), the card header shows a star toggle, and the
  library toolbar has a frontend-only **Favourites** filter.
- **Export** (`frontendExportSnippet`) opens a save dialog for WAV/AIFF/FLAC
  (`SnippetLibrary::exportSnippetToFile`, `getSupportedExportExtensions`) and **bakes** `gainDb`
  into the exported file (an export is a finished file), while the library save stays at unity —
  make that distinction explicit in the UI copy.

## Takes list, setlists and bulk selection (0035)

- The takes list (`SnippetList.jsx`) has a toolbar: string search (name/comments/notes/key), sort
  (newest/oldest/name/longest/shortest), key filter (distinct detected keys), a **Favourites**
  toggle (`favouritesOnly`, frontend-only), and tag-colour filter chips (multi-select, incl.
  untagged) — all frontend-only, with the grid capped at 10 takes + a "Show more" button; **Named
  setlists** (0035, `Source/SetlistStore.h/.cpp`, unit-tested): ordered, duplicate-free lists of
  snippet ids persisted as a JSON array under the `setlists` properties key (debounced write like
  `tagNames`, read in `restoreUserState`, pruned against the library on load and on snippet delete).
  Events `frontendCreateSetlist { name }`, `frontendRenameSetlist { id, name }`,
  `frontendDeleteSetlist { id }`, `frontendAddSnippetToSetlist`/`frontendRemoveSnippetFromSetlist { setlistId, id }`,
  `frontendSetSetlistOrder { id, ids }`; the snippets payload ships `setlists: [{id, name, ids}]`.
  `SnippetList` gets a setlist filter (its order is authoritative in that view) and a Setlists
  manage popover (create/rename/delete, inline prompts — WebView2 has no native dialog);
  `SnippetCard` shows a Setlists chip row and, in a setlist-filtered view, up/down reorder controls.
  A deleted or missing snippet leaves every setlist; **bulk selection** (0035): a toolbar **Select**
  toggle puts a checkbox on each card and a bulk bar (select all shown, set colour on the selection,
  add the selection to a setlist, delete with a two-step confirm). Bulk actions use dedicated events
  (`frontendSetSnippetsColor { ids, color }`, `frontendAddSnippetsToSetlist { setlistId, ids }`,
  `frontendDeleteSnippets { ids }`) so the backend persists once and fires a single
  `libraryChanged`; the selection is pruned when snippets disappear; card summaries use two rows
  (name on its own line, key/notes/duration/date below) so long names aren't clipped.

## Audio import (0024)

- **Import** (0024): `frontendImportAudio` opens a multi-select chooser and `frontendImportAudioData { name, data }`
  imports a drag-dropped file — WebView2 (and JUCE's `WebBrowserComponent`) exposes no dropped-file
  path, so the React drop handler reads `File.arrayBuffer()` and base64s it (drops capped at 32 MB;
  the chooser has no cap). `SnippetLibrary::importAudioFile` / `importAudioFromStream` decode
  (WAV/AIFF/FLAC/OGG/MP3 via `AudioFormatManager`), compute peaks, and store the source (absolute
  path, or `drop:<name>:<size>` for a dropped file) in `savedPath` so re-importing the same source
  is a no-op; a 30-minute cap bounds the decode. The library never overwrites an imported file (it
  is referenced in place).

## Header tools: Scales and Chords

- The header **Scales** popover (0053, `PracticeScales.jsx`) reuses the same `frontendSetTunerOpen`
  analysis (it never touches `tunerMonitorMute`) and is mutually exclusive with the Tuner — one
  frontend `activeTool` owns the popovers, seeded from the persisted `tunerOpen` on init and
  reconciled when a late host-state restore flips the authoritative backend flag (open keeps the
  current tool or defaults to the Tuner so a running worker always has an owner; closed clears it),
  so the shared flag can't make ownership ambiguous; it draws the scale on a standard-tuning
  fretboard using pure theory in `WebUI/src/utils.js` (five scales, key-signature accidentals incl.
  minor relative-major spelling), prefilled from the playing snippet's detected key (retried if the
  key payload lags the playing id, with a manual choice locked for that source and a reopen
  re-prefilling) and highlighting the live tuner note. The header **Chords** popover (0058,
  `Chords.jsx`) is a third, **non-analysis** tool in the same `activeTool` slot: opening it closes
  Tuner/Scales and stops their worker through the existing `frontendSetTunerOpen` close, but it adds
  no new backend event and owns no tuner state. It renders a one-octave piano and a scrollable
  standard-tuning fretboard; clicked pitch classes feed pure theory in `WebUI/src/chords.js`
  (`ChordTheory` CTest) that builds diatonic/borrowed chord catalogues, ranks suggestions by
  selected-tone overlap, identifies common chord shapes, and spells borrowed tones from the key
  (e.g. E major's G# in A minor). The key/scale/7ths/selection state is session-only React state (no
  `localStorage` or host state); selecting a chord suggestion replaces the selection with its chord
  tones. Its **Find scales** view reverses the workflow: two or more selected pitch classes rank
  every root+scale pair that contains them (`findScaleMatches`, full fits first, tighter scale
  first), and selecting a suggestion previews that scale's notes without changing the selection.
  **Chord set**: each suggestion (and the prominent top **Detected chord** card) can add its chord
  to a session-only collection (`chords.chordSet`), shown as chips that re-inspect the chord when
  clicked; `scalesForChords` (pure, unit-tested) intersects the collected chords' tones and lists
  every scale containing all of them as clickable chips, in both modes, with a Clear set action.

## Accessibility (0017)

- **Accessibility** (0017): every interactive control is keyboard-operable and labelled — a global
  `:focus-visible` ring, `Knob` as a `role="slider"` with `aria-valuetext` +
  Arrow/PageUp-Down/Home/End keys (pointer/keys no-op when disabled), `LevelMeter` as a
  `role="meter"` in dBFS, WAI-ARIA tabs, `role="alert"` error toasts with a keyboard-reachable
  dismiss button, and a resize grip that is a real button (arrow keys resize) — and cosmetic
  animation is disabled under `prefers-reduced-motion` (the meter/playhead transitions are kept
  because they track live audio). Conventions live in the **webui-bridge** skill's Accessibility
  section.

## Window: aspect-locked corner grip and maximise (0060/0062)

- **Resizable, aspect-locked window (corner grip)**: the editor keeps a fixed 960:700 aspect ratio
  (`WebViewEditor.h` `designWidth`/`designHeight`/`designAspect`; 0.7x–2x limits) via
  `setResizeLimits` + `getConstrainer()->setFixedAspectRatio`, then **`setResizable(false, false)`
  disables the OS/host resize border and JUCE's built-in corner** — WebView2 is a native child HWND
  that occludes JUCE-drawn components, so the only resize affordance is the WebUI's own **corner
  grip** (`ResizeHandle.jsx` / `.bp-resize-handle`, a 44px bottom-right hit area with a diagonal
  hatch and `nwse-resize` cursor). It emits `frontendResizeEditor { dWidth }` (a device-pixel width
  delta computed from `screenX`, **not** `clientX` — client coords would feed the resize back into
  itself as the window grows under the pointer); `WebViewEditor::handleResizeEditor` clamps the
  width to the limits, re-derives the height from the locked ratio and `setSize`s the editor, and
  the standalone wrapper resizes its window to contain it (`MainContentComponent` listens to the
  editor; a DAW may ignore plugin-initiated resizes). Because the ratio is locked, only the
  horizontal drag component changes the size. Window size persists in the properties file
  (`editorWidth`/`editorHeight` via `getSavedEditorWidth`/`saveEditorSize` — the JUCE standalone
  only remembers the window *position*), debounced 400 ms in the editor `timerCallback` and flushed
  on destruction; the width is authoritative (height is re-derived from the locked ratio). A
  standalone-only **Maximise/Restore** control (0060/0062, `frontendSetMaximised { enabled }`, also
  `F11`) grows the top-level `DocumentWindow` to **fill the display's `userArea`** (minus the
  current window chrome) and centres it, keeping the OS title bar — deliberately **not** true
  fullscreen. The window may be any aspect: while maximised the editor relaxes its constrainer
  (`setFixedAspectRatio(0)` + a large `setSizeLimits`) so the standalone wrapper accepts the full
  rectangle, and re-applies the 0.7×–2× + 960:700 limits on restore or when a grip resize leaves
  maximise. `Source/EditorWindowMath::computeFillContentSize` (pure, CTest
  `test_EditorWindowMath.cpp`) computes the target; the WebUI's Fit mode then stretches the design
  surface to the available width. The pre-maximise size/position are restored on toggle, and the
  debounced size persist is suppressed while maximised (quitting maximised saves the pre-maximise
  size). In a DAW the control is hidden (`standalone` in the transport snapshot) and the event is
  inert.

## UI scaling and interface zoom (0060)

- **UI scaling**: the React UI is laid out at a **minimum** 960px design width and scaled as a whole
  by `UiScale.jsx` with **`transform: scale()`** (`translateX(offsetX) scale(s)`, origin top-left),
  where `s = min(scrollClientWidth/960, scrollClientHeight/700)` is measured from the `.ui-scroll`
  element, **never** `window.innerWidth` (innerWidth includes the vertical scrollbar). `transform`
  is deliberate over the CSS **`zoom`** property: Chromium's `zoom` is under-specified and
  **mis-paints dynamically-updated content** — pressing a control (e.g. a chain **Solo**) could
  leave the lower part of the window blank until a forced repaint, which is why the shell moved off
  `zoom`. Two `transform` consequences are handled: (1) transforms don't affect layout, so
  `.ui-scaled` reserves the scaled scroll size and a `ResizeObserver` keeps it in sync as content
  grows/shrinks (e.g. the Loop tab is taller than Take); (2) a transformed element becomes the
  containing block for `position: fixed` descendants, so **the splash and the notification toast are
  portaled to `document.body`** (`createPortal` in `App.jsx`) and render unzoomed at a constant
  default size (the splash no longer scales with the window). **`.ui-content` is a flex column and
  `.app` is its flex item, so `.app` needs `width: 100%; min-width: 0`** — with the default
  `min-width: auto` the app grows to its content's min-content width, which is font-metric-dependent
  and on a real Windows WebView2 (~1112px) pushed the whole UI wider than the viewport so the right
  edge (e.g. the Monitor knobs) was cut off; headless Chrome's different fonts did not reproduce it,
  so verify layout changes in a real WebView2. `.ui-scroll` owns the vertical scrollbar and uses
  `scrollbar-gutter: stable` so its measured client width is constant; `.ui-content` sets inline
  `min-height = clientHeight / scale`. Viewport-width `@media` reflow breakpoints were removed (the
  layout is sized for the 960 minimum: single-row masthead, tagline hidden), and the few `vw`-based
  `clamp()`s were pinned to fixed base values. **Fit uses the width (0062):** the content width is
  `max(960, viewportWidth / scale)` (and `.app` has no max-width), so a wide or maximised window
  gives the chain rail more cards and the library grid more columns instead of side gutters; manual
  zoom stays absolute at 960 design px (centred or two-axis scrolled). **WebView2 page-zoom is
  suppressed in `main.jsx`** (non-passive `wheel`/`keydown` `preventDefault` on Ctrl/Cmd — JUCE
  exposes no zoom-control option): page zoom changes the CSS pixel, which would resize the corner
  grip while the self-scaling UI compensates, so Ctrl+wheel looked like it only re-sized the grip.
  **Interface zoom (0060)**: `Fit` is the default; a manual zoom is absolute — 100% = the 960 CSS px
  design width — and persists in `localStorage` (`blueprinter.uiZoom`). Below Fit the fixed-width
  surface is centred and shows more vertical content; above Fit `.ui-scroll` scrolls both axes
  (`.ui-scaled` reserves the scaled width and height). `Ctrl/Cmd + wheel`, `Ctrl/Cmd + - / =`, and
  `Ctrl/Cmd + 0` (Fit) drive it; `Knob`/`Looper`/`PianoRoll` wheel handlers ignore Ctrl/Cmd so the
  gesture stays global. The zoom + maximise controls render in the **unscaled** `.ui-shell-controls`
  cluster beside the grip, outside the transformed content. `UiScale` also exposes `useUiScale() { scale, offsetX, overlayNode }`
  for the 0059 sticky quick bar's pre-scaled overlay. The during-splash scroll lock is
  `html:has(.bp-splash) .ui-scroll { overflow: hidden }`.

## Startup splash

- **Startup splash**: `SplashScreen.jsx` covers the startup wait with real progress. It renders from
  first React paint (`showing` → `leaving` fade → `hidden`) and stays until (a) the first
  `backendVst3Chain` snapshot arrives AND (b) that snapshot reports `restoring: false` (i.e. the
  deferred plugin restore finished; no snapshot = "Starting up…" indeterminate bar). It re-appears
  if a later host state load starts another restore. Progress comes from the per-chain `pending`
  counts in the chain snapshot (`loaded = Σ slots − pending`); the splash also shows per-chain chips
  for chains with pending loads and the `restoreError` line. The load phase shows a determinate ink
  fill plus an "≈Ns left" ETA extrapolated from serial-slot throughput (see `etaSec` in `App.jsx`);
  **once `remaining` hits 0 the splash enters the `is-finishing` tail phase** ("Applying saved
  state" + spinner + a renderer-side elapsed clock) — the last plugin's `setStateInformation` blocks
  the C++ message loop (no snapshot events, no progress source), but JS timers keep running in
  WebView2 so the clock still ticks. When the deferred restore fully drains, the restore driver now
  fires `pluginChainChanged()` (its crash-marker-clear path) so a fresh `backendVst3Chain` snapshot
  with `restoring: false` reaches the UI and the splash leaves promptly — before that fix nothing
  pushed a snapshot after the last state applied, so the splash could linger in the tail on a stale
  `restoring: true` even though the restore was already done. `App.jsx` emits `frontendGetVst3Chain`
  on mount (coalesces with `PluginChain`'s) so the splash learns the restore state early; a
  re-appearing splash mid-session (DAW project load) is intentional.
- The WebView2 boot gap before the page loads is covered by the min display time (`SPLASH_MIN_MS = 900`)
  plus a **native JUCE splash**: the editor now keeps `webView` hidden (`addChildComponent` +
  `setVisible(false)`) and paints `drawStartupSplash` (paper, wordmark, indeterminate ink sweep)
  until `BluePrinterWebViewComponent::onPageFinishedLoading` (or a 4 s timer fallback) reveals it —
  the WebView2 child HWND only occludes parent painting while visible, so hiding it lets JUCE paint
  the gap. `revealWebView` re-arms `chainUpdatePending` because events emitted while hidden are
  dropped by `emitEventIfBrowserIsVisible`, and the WebView2 default background is set to the paper
  colour (`0xfff1eee6`) so a late paint never flashes white; `makeWebViewOptions` is also built only
  once (the editor's support probe uses a cheap backend-only options object instead of rebuilding
  the initial snapshots). The WebView2 user-data cache folder is already persisted to speed up
  boots.

## Song builder (0065)

- **Song builder** (0065): a third **Song** tab beside Take and Loop — a *planner*, not a timeline
  (ADR-0008). A song is a document (`Source/SongStore.h/.cpp`):
  `{version,id,name,keyRoot,keyScale,bpm,numerator,denominator,sections:[{id,name,bars,chords:[{bar,symbol,name}],idea,snippetId,recordedBpm,recordedBars}]}`.
  Limits: 64 songs, 64 sections/song, 512 total bars/song, section bars normalised to
  {1,2,4,8,12,16}, at most 32 chord events/section (unique bars, sorted, inside `[0,bars)`), name
  1..80, section name 1..40, idea 0..200, symbol 1..12, chord name 0..40, bpm 20..300, numerator
  1..16, denominator {1,2,4,8,16}, keyRoot 0..11, keyScale major/minor, snippetId 0/positive,
  recordedBpm 0 or 20..300, recordedBars 0..16, ids `[A-Za-z0-9_-]{1,32}` with deterministic
  `s1`/numeric fallbacks. Persisted as a JSON array under the `songs` properties key with the same
  500 ms debounce as setlists (`armSongPersist`/`flushSongPersist`); absent = empty.
  `restoreUserState` loads it after setlists and prunes links to missing snippets; every
  `pruneSetlistsAgainstLibrary()` call site also calls `pruneSongsAgainstLibrary()` and coalesces
  `libraryChanged`. Events: `frontendSaveSong { song }` and `frontendDeleteSong { id }` (no new
  backend event — `songs` rides `backendSnippets` and the initial data blob).
  `frontendCreateSetlist` now takes optional `ids` so the Song footer can create an ordered setlist
  of the linked recordings. `WebUI/src/songModel.js` is the pure model (templates
  `pop`/`blues`/`blank`, bar/duration math, editing helpers, `suggestChords` via
  `buildChordCatalog`, `chartText`), covered by the `SongModel` CTest; `Song.jsx` is the tab.
- Recording a section applies the song tempo/meter, sets `frontendSetLooperLengthBars { bars: section.bars }`,
  switches to the Loop/Take tab and starts the capture, remembering the section; **Save & attach**
  saves the loop/take and, on the next snippets payload, diffs the ids, links the new snippet, sets
  `recordedBpm`/`recordedBars` (from `durationSeconds` at the song's bar length) and renames an
  autogenerated `Snippet …`/`Loop …` name to `<song> - <section>` (comments preserved). **Link**
  picks an existing snippet (`recordedBpm` 0, bars estimated). Playback reuses
  `frontendStartPlayback`/`frontendStopPlayback` and `frontendSetSnippetMelodyPlayback`. The active
  song persists in `localStorage` (`bp:activeSongId`), the tab in `bp:recordingMode`
  (`take`/`loop`/`song`). While the Song tab is active, header BPM/meter changes write back to the
  song (400 ms debounce); selecting a song applies its BPM/meter once. **Space** is owned by `Song`
  (records the selected section as a fixed-length loop; the App handler ignores it in song mode);
  Enter/Delete stay take-only. The looper's Length select now offers 12 and 16 bars too (the backend
  already accepted 0..16).

## Song rehearsal (0066)

- **Rehearsal** (0066, ADR-0009) is a click-only transport, not song playback:
  `frontendSetPracticeRunning { enabled, startSample? }` sets a session-only `practiceRunning`
  atomic, and while it is set the free-run block advances `metronomePosition` and sounds the click
  (subject to `metronomeEnabled`, never `clickDuringCapture`) while the MIDI pulse output stays
  gated on `clockRunning` alone — a rehearsal never sends MIDI. Enabling is refused during a
  take/loop capture and stops snippet playback, take review, loop playback and the melody audition;
  `startRecording`, `setLooperRecording(true)`, `startPlayback`, `setTakePlayback(true)`,
  `setLooperPlaying(true)`, `setSnippetMelodyPlayback(..., true)`, `setMelodyPlayback(..., true)`,
  `releaseResources` and the editor destructor clear it. The transport snapshot ships
  `practiceRunning` + `practicePosition` (the metronome position in samples). The Song tab's
  rehearsal bar starts at the section's **first bar** (`rehearsalStartSample(song, index, barSamples)`
  = `sectionStartBar * barSamples`, *Rehearse from here* / *Rehearse song*), and that first bar is
  the **one full bar** lead-in when `countInBeats > 0`, so the song itself starts on the next
  downbeat with the meter accents aligned. The pure `rehearsalAt(song, practicePosition, { startSample, leadInSamples, barSamples })`
  maps the position **relative to the run's start sample** (never from bar 0): `relative < leadInSamples`
  is `countin` on the started section, otherwise `songBar = floor(startSample / barSamples) + floor((relative - leadInSamples) / barSamples)`;
  it returns `{ phase: "countin"|"playing"|"done", sectionIndex, barInSection, barInSong, chord }`
  (the held chord). The tab stores the start sample in session state (restored on remount, since the
  Song tab unmounts on a tab switch), highlights the current arrangement block / section row / chord
  cell with `is-current` (distinct from the user's `is-selected`), shows the section, bar counter,
  big chord and count-in beat, and emits the stop once at `done`; an App-level safety effect does
  the same from any tab (`lead + totalBars * barSamples` is the absolute end for any section start);
  Esc and Stop end it. No host-state key, no audio-path change. Files: `Source/SongStore.h/.cpp`,
  `Tests/test_SongStore.cpp`, `WebUI/src/songModel.js`, `WebUI/src/components/Song.jsx`,
  `Tests/check-song-model.mjs`, `docs/adr/0008-song-builder-is-a-plan.md`,
  `docs/adr/0009-rehearsal-is-a-click-not-a-sequencer.md`.
