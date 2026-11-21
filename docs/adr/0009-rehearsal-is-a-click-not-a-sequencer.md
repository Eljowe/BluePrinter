# Rehearsal is a click, not a sequencer

The Song tab gained a **rehearsal** transport (0066): *Rehearse from here*
starts the click at the selected section, *Rehearse song* at the top, and the
tab highlights the current section, bar and chord as the plan moves. It is a
**click-only transport**, not song playback. Nothing from the library sounds
during a run: no linked recording, no loop, no take, no melody audition.

## Why not play the plan

A song already knows which recording belongs to each section (ADR-0008), so the
obvious next step looks like "play them in order". It isn't, for the same
reasons the plan has no timeline:

- snippets carry no tempo or bar metadata, so a linked recording cannot be
  placed on the grid without time-stretching (out of scope) or drifting against
  the click;
- sections link one recording each, but a rehearsal needs a bed the player can
  play *over*, and the existing snippet playback is a one-shot audition that
  replaces the live input;
- sequencing would need a region model, a gapless scheduler and a transport
  that owns the audio path — a DAW inside the plugin, which ADR-0008 already
  rejected.

What a player actually needs while learning a part is the **click and the
chart**. The chart is already the document; the click is already the metronome.
Rehearsal connects the two and stops there.

## Reusing the beat clock

`metronomePosition` is the existing continuous beat clock. A take/loop capture
and the free-running MIDI clock already advance it; the free-run path sounds
the audible click and renders no MIDI unless the clock is running. Rehearsal
adds one session-only `practiceRunning` atomic to that same free-run condition:

- while it is set, `processBlock` advances `metronomePosition` by the block
  and sounds the click (subject to the master `metronomeEnabled` toggle);
- the MIDI pulse output stays gated on `clockRunning` alone, so a rehearsal
  never drives external gear;
- `practicePosition` in the transport snapshot is that same position, and the
  frontend maps it to a section/bar/chord with pure `songModel.js` math.

No new audio path, no second clock, no new capture geometry. The click obeys
the click sound settings and `metronomeEnabled` only — not
`clickDuringCapture`, which is about printing the click into a take.

## Count-in is one full bar

When the take count-in is on (`countInBeats > 0`), a rehearsal starts with **one
full bar** of lead-in on the section's first bar, and the song begins on the
next downbeat. The lead-in is a whole bar, not `countInBeats` beats, because the
click accents are per-bar: a partial lead-in would put the song's first beat on
an unaccented beat and the meter would sound wrong for the whole run. The
frontend shows the lead-in beat number and highlights the section the run
started in, so the player knows where they are.

## Start points, end, exclusivity

A run starts at the **section's first bar**: *Rehearse from here* at the selected
section's first bar, *Rehearse song* at bar 0. That bar is the lead-in when the
count-in is on, so the song itself starts one bar later, on the next downbeat.
The frontend maps the beat clock **relative to that start sample** (never from
bar 0), so a later-section run counts in on, and starts from, the section it was
launched on. Both run to the end of the song and stop there; the absolute end is
`leadIn + totalBars * barSamples` for any start section, and both the Song tab
and an App-level safety effect emit the stop once when the position passes it
(the Song tab unmounts on a tab switch, so the App effect is what makes
"stops at the end" hold from Take/Loop too). Section looping, tempo changes
mid-run and a footswitch are out of scope.

The transport is exclusive: a take or loop capture stops rehearsal (and a
rehearsal is refused while a capture is active), and starting a rehearsal stops
snippet playback, take review, loop playback and both melody auditions — none of
them are synced to the click, so leaving them running would be noise. Starting
any of those likewise stops the rehearsal.

## Session-only

Rehearsal is a practice action, not a document property: no host-state key, no
properties-file entry, no position restore. The Stop button and `Esc` end it;
`releaseResources` and the editor destructor clear the flag so a stopped device
or a closed window can never leave a runaway click.

## Consequences

- One new frontend event (`frontendSetPracticeRunning { enabled, startSample? }`)
  and two transport-snapshot fields (`practiceRunning`, `practicePosition`); no
  new backend event.
- The frontend pure model grows `rehearsalAt` and `rehearsalStartSample`, covered
  by the `SongModel` CTest, so the bar/section/chord mapping is testable without
  a WebView.
- The Song tab gains a rehearsal bar (start/stop, section/bar/chord readout,
  count-in beat, click-off hint) and `is-current` highlights distinct from the
  user's `is-selected`.
- The print, stems, PDC, chain routing and every existing state key are
  untouched.
