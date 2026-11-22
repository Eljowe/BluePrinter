# BluePrinter

A cross-platform guitar take-recorder plugin (VST3 + Standalone; AU on macOS)
that hosts parallel VST3 FX chains, records takes and loops from the processed
signal, and files them in a local snippet library. This file defines the
project's language and shape; the always-loaded rules and the map live in
[`AGENTS.md`](AGENTS.md), and implementation mechanics live in
[`docs/architecture/`](docs/architecture/README.md).

## Language

### Capture and playback

**Take**:
A one-shot recording of the recording mix into the shared record buffer.
_Avoid_: Recording, clip

**Take stack**:
Every stopped take is retained (bounded to 8 takes / 256 MB) in `TakeRecorder`,
each owning its own audio. One take is selected for review; takes are session-only
until saved as snippets. A new capture appends; it does not invalidate the stack.
_Avoid_: Pending take (removed — the stack replaces the single slot), take buffer

**Take overdub**:
Layering a new pass over the **selected** take instead of adding a new one (the
Take review's Dub toggle, session-only). The selected take is staged into the
record buffer, the new input is captured after it and wrap-mixed into it on stop,
so playback and the saved WAV contain every layer. With a trim, only the kept
region is looped and mixed; audio outside it stays intact. Completed overdubs
can be undone per take (10 recent versions / 64 MB shared history, session-only).
_Avoid_: Punch-in, comping, takes-stacking

**Take trim**:
A non-destructive kept window of the full take, expressed in absolute source
samples `[start, end)`. Review playback and overdub follow that window; saving
materialises only it as a snippet (and clips/shifts cached melody notes). The
full waveform remains available until save/delete; Reset trim restores it.
_Avoid_: Crop (reserved for the beat-based loop controls), destructive edit

**Loop**:
A bar-aligned, repeatable capture of the recording mix, optionally cropped to
whole beats.
_Avoid_: Loop region, backing track

**Recording mix** (also *capture mix*):
The signal actually printed: the dry input scaled by Dry, plus the outputs of
chains whose Record toggle is on. Takes and loops both tap it. The dry and every
chain are latency-aligned by PDC, and a capture skips the leading L-sample flush
(capture offset) so the saved audio starts on the armed downbeat.
_Avoid_: Record bus, wet mix

**Capture offset**:
The leading L samples of the PDC-delayed print that a capture skips so the
saved take/loop begins on the armed downbeat rather than L samples late.
Fixed-length auto-stop counts written samples, and the per-chain stems share the
same offset.
_Avoid_: Latency trim, pre-roll (that is the count-in)

**Dry**:
The direct, unprocessed input scaled by the Dry level, present in both the monitor
mix and the recording mix. Chains always receive the unscaled input.
_Avoid_: DI, clean signal

**Overdub**:
Recording a new layer over an existing loop instead of replacing it, mixed in on
stop by `mixOverdubLayer`.
_Avoid_: Layer, comp

**Count-in**:
A click-only pre-roll of N beats before a capture starts.
_Avoid_: Pre-roll (reserved for the processor's `preRollActive` state)

**Click**:
The audible metronome, gated by the master Click toggle and the Click: capture
toggle.
_Avoid_: Metronome (the APVTS-independent settings object is `metronomeEnabled`)

**Bounce**:
Materialising a loop as a new library snippet.
_Avoid_: Export (export writes a file; bounce creates a snippet)

**Loaded loop**:
A library snippet copied into the looper (resampled to the session rate) so it
can be overdubbed. The snippet is never modified; saving the result makes a new
`<source> overdub` snippet. The loop remembers its source name (`looperSourceName`)
so the UI can show a "Loaded:" chip.
_Avoid_: Import (import adds a snippet to the library, it does not seed the looper)

**Melody**:
The monophonic note events extracted from a finished take or a library snippet
(pitch track → segmentation) together with the detected key. A take's melody is
session-only until the take is saved, when it is written into the snippet's
sidecar; a library snippet can be analysed in place (`analyzeSnippetMelody`),
persisted to its sidecar, and auditioned from its card.
_Avoid_: Transcription (implies polyphony/notation), tab

### Routing

**Chain**:
An independently named, parallel VST3 processing path with its own input mask,
MIDI routing, Record toggle, volume, mute, and monitor state.
_Avoid_: Bus, track, strip

**Slot**:
One plugin instance within a chain, ordered and individually bypassable.
_Avoid_: Plugin, insert

**Retired chain / slot**:
A chain or slot that has been removed from the live list but is not destroyed
yet. It is held until two snapshot refills have happened since the removal (the
first drops it from the audio thread's snapshot, the second confirms), and is
then freed on the message thread. Never released on a wall-clock timer, so a
paused audio callback cannot make it drop the last reference inside the
callback.
_Avoid_: Graveyard, tombstone, zombie

**Input mask**:
The subset of the 1–8 channel input bus a chain reads. A chain never reads another
chain's output.
_Avoid_: Channel map

**Monitor mix**:
What the user hears, post-master-Output. Monitor-only Solo and monitor-mute change
it; they never change the recording mix. It is latency-aligned by PDC: the dry
path and each chain are delayed to a common L.
_Avoid_: Main mix (that is the pre-Output sum)

**Plugin delay compensation (PDC)**:
The alignment of the dry pass-through and every parallel chain to one uniform
latency `L`, the maximum over the loaded, non-bypassed, non-hard-muted chains.
The dry path waits `L`, each chain waits `L - its own latency`; both the monitor
and the print are aligned. `L` is reported to the host and latched for the
duration of a capture. Capped at 500 ms with one notification.
_Avoid_: Latency compensation, auto-align

**Guard** (monitor):
The optional, zero-latency soft clip at the very end of the monitor path — after
the master Output and the tuner mute, immediately before the OUT meter. It bounds
each channel at a dBFS ceiling (default −1 dBFS) with a fixed soft-knee curve and
latches the OUT clip LED when it has to limit. The print and the record meter are
never touched.
_Avoid_: Limiter (there is no lookahead or delay compensation), compressor

**Blocklist** / **Quarantine**:
Two different exclusions. A *blocklist* entry is a plugin the user chose to skip
in the scanner. A *quarantine* entry is a plugin the self-heal added after it
crashed a restore or load.
_Avoid_: Blacklist for quarantine

### Library

**Snippet**:
An in-memory library item: audio plus metadata (name, comments, colour, key,
playback gain). A take or loop becomes a snippet when saved.
_Avoid_: Sample, item, take (a take is pre-save)

**Sidecar**:
The JSON file written beside a snippet's WAV holding its metadata.
_Avoid_: Metadata file

**Gain** (snippet):
A non-destructive playback trim in dB, never baked into the WAV.
_Avoid_: Normalize (that sets Gain so the peak lands at −1 dBFS)

**Tag** / **Colour**:
An organisational colour key on a snippet; the user-facing name of a colour is a
per-user Tag name stored once, not per snippet.
_Avoid_: Label, category

### Song planning

**Song**:
A planning document: name, key (root + major/minor), tempo, meter and an
ordered list of sections. It is a plan, not a timeline — no audio regions, no
mixer, no gapless sequencing. Persisted under the `songs` properties key and
shown in the Record section's third tab.
_Avoid_: Project, session (the DAW's), arrangement (that is the strip of
section blocks)

**Section**:
One ordered part of a song: a name, a loopable bar count (1/2/4/8/12/16), its
chord events, a melody/idea line, free multi-line lyrics and one linked
recording. A section always maps to one fixed-length loop.
_Avoid_: Part, region, clip

**Chord event**:
One chord placed at a bar of a section. A chord holds from its bar until the
next event; later bars read as a held dot. At most one event per bar, sorted,
inside the section's bar range.
_Avoid_: Chord block, chord region

**Capture attach**:
Saving the current loop or take from the Song tab and linking the new snippet to
the section in one action: the new snippet's id is diffed against the previous
library payload, the section stores `snippetId`/`recordedBpm`/`recordedBars`,
and an autogenerated snippet name is renamed to `<song> - <section>`.
_Avoid_: Bounce (that is materialising a loop as a snippet), export

**Rehearsal**:
A click-only transport over a song's plan (0066): the metronome runs the
arrangement and the tab highlights the current section, bar and chord. It is
not song playback — no linked recording, loop, take or melody sounds. Published
in the transport snapshot as `practiceRunning` (bool) and `practicePosition`
(the metronome beat position in samples); session-only, started with
`frontendSetPracticeRunning` and stopped by Stop, `Esc`, a capture or the
editor closing.
_Avoid_: Playback, playthrough, song play

### Session and state

**Properties file**:
`%APPDATA%\Retrokielto\BluePrinter.properties` — BluePrinter's own user state
(chains, quarantine, metronome, editor size).
_Avoid_: Settings

**Settings file**:
`%APPDATA%\BluePrinter\BluePrinter.settings` — the JUCE standalone's own state
blob, written only on clean exit. Distinct from the properties file; deleting it
is safe.
_Avoid_: State file

**Deferred restore**:
Loading saved chain slots one plugin per message-loop turn, never synchronously.
_Avoid_: Lazy load

**Self-heal**:
Skipping state-blob restore and/or quarantining a plugin after a crash, so the
next launch does not repeat it.
_Avoid_: Recovery

### UI

**Design size**:
The fixed 960×700 layout the React UI is authored at.
_Avoid_: Base size

**UI scale**:
The single `transform: scale()` factor applied to the whole UI from the measured
scroll viewport. Never CSS `zoom`.
_Avoid_: Zoom

**Practice scale**:
The scale shown in the header **Scales** popover on a standard-tuning fretboard.
The theory is pure JS (`WebUI/src/utils.js`): a root + scale id yields note
names/degrees with key-signature accidentals (a minor scale borrows its
relative major's spelling). It reuses the tuner's live analysis but is a
separate, mutually exclusive tool.
_Avoid_: Key (that is the detected snippet key), mode

**Chord scratchpad**:
The frontend-only songwriting tool in the header **Chords** popover. A key +
scale highlights compatible notes on a one-octave piano and a standard-tuning
fretboard; selected pitch classes feed ranked diatonic/borrowed chord
suggestions. Its **Find scales** view reverses the workflow: selected notes
rank the root+scale pairs that contain them, and choosing one previews its
tones without changing the selection. It makes no sound, starts no tuner
analysis, and persists nothing.
_Avoid_: Chord detector (there is no audio/MIDI chord detection), progression
builder

**Bridge event**:
A named frontend↔backend message, declared once in `WebViewEditor.h` and mirrored
in `bridge.js`.
_Avoid_: Message, IPC event

## Module map

| Area | Where |
| ---- | ----- |
| Architecture docs (on-demand mechanics moved out of AGENTS.md) | `docs/architecture/` |
| Signal flow, chains/routing, PDC, capture, takes, stems, meters, clock | `docs/architecture/audio-path.md` |
| Chain persistence, deferred restore, self-heal, presets, diagnostics | `docs/architecture/chain-restore.md` |
| React shell, bridge payloads, scaling, splash, library/songs UI | `docs/architecture/webui.md` |
| Build, WebView2 patch, ASIO, tasks, CI, packaging/release | `docs/architecture/build-and-ci.md` |
| Audio processor, routing, state, restore | `Source/PluginProcessor.h/.cpp` |
| WebView2 editor + bridge dispatch | `Source/WebViewEditor.h/.cpp` |
| VST3 chain + async loading | `Source/PluginChain.h/.cpp` |
| VST3 scanning | `Source/Vst3Library.h/.cpp` |
| Snippet library + WAV/JSON | `Source/SnippetLibrary.h/.cpp` |
| Key detection | `Source/KeyDetector.h/.cpp` |
| Take melody extraction (0054) | `Source/MelodyAnalyzer.h/.cpp` |
| Melody audition synth (0054) | `Source/MelodyPlayer.h/.cpp` |
| Metronome click synthesis | `Source/ClickSynth.h/.cpp` |
| Metronome click scheduling + ring-out | `Source/MetronomePlayer.h/.cpp` |
| MIDI clock pulse scheduling | `Source/MidiClockMath.h` |
| Loop playback + overdub wrap-mix | `Source/LoopPlayback.h` |
| Looper state: capture, grid trim, crop, undo/redo | `Source/Looper.h/.cpp` |
| Crash self-heal: blob gating, suspects, quarantine | `Source/RestoreSelfHeal.h/.cpp` |
| Chain bundle serialise/apply + chain construction | `Source/ChainStatePersistence.h/.cpp` |
| Chain input select + monitor/record sum | `Source/ChainRouting.h` |
| PDC latency maths (0046) | `Source/DelayCompensation.h` |
| Preallocated alignment delay line (0046) | `Source/DelayLine.h` |
| Capture-buffer write clamp | `Source/CaptureWrite.h` |
| Capture-region copy (finalize) | `Source/CaptureCopy.h` |
| Snippet→looper rate/channel prep (0056) | `Source/Resampler.h/.cpp` |
| Count-in completion test | `Source/PreRollMath.h` |
| MIDI output device + direct sends | `Source/MidiClockOutput.h/.cpp` |
| Lock-free MIDI send queue (audio → sender thread) | `Source/MidiSendQueue.h` |
| Metering math | `Source/MeterMath.h` |
| Per-chain CPU math (0047) | `Source/ChainCpuMath.h` |
| Per-meter level/peak/clip state | `Source/Meter.h` |
| Monitor soft-clip guard curve (0048) | `Source/SoftClip.h` |
| Standalone window fill maths (0062) | `Source/EditorWindowMath.h` |
| Take stack + review/overdub state machine | `Source/TakeRecorder.h/.cpp` |
| Per-chain capture stems (0038) | `Source/StemCapture.h/.cpp` |
| Named setlists (0035) | `Source/SetlistStore.h/.cpp` |
| Song documents + sanitising (0065) | `Source/SongStore.h/.cpp` |
| Song model: templates, bar math, suggestions, chart (0065) | `WebUI/src/songModel.js` |
| Song tab UI (0065) | `WebUI/src/components/Song.jsx` |
| React app + components | `WebUI/src/` |
| Bridge API | `WebUI/src/bridge.js` |
| Chord scratchpad theory + ranking (0058) | `WebUI/src/chords.js` |

## Invariants

- The audio thread never blocks and never allocates inside `processBlock`: chain
  and slot snapshots are refilled with `tryEnter()` (falling back to the previous
  block's snapshot), MIDI output is queued lock-free, and memory is preallocated.
- Removing a chain or a slot never frees it immediately: it is retired and
  reclaimed on the message thread once two snapshot refills have happened since
  the removal (never on a wall-clock timer), so the audio thread can never hold
  a dangling chain, slot or plugin — and can never be the thread that destroys
  one. `prepareToPlay`/`releaseResources` force the collection with audio stopped.
- Chains run in parallel on a pristine input snapshot and never hear each other.
- PDC aligns the dry path and every chain to one uniform `L` (preallocated delay
  lines, latency read under a try-lock). `L` is latched during a capture and the
  capture cursor skips `L`; the click/loop/playback/MIDI stay on the true grid.
- Monitor controls (Output, Solo, monitor-mute, Loop level, Guard) never change the print.
- Melody audition is monitor-only; it is stopped when a capture starts and never
  enters the recording mix. One melody player at a time (`MelodySource` tracks
  whether a take or a library snippet is sounding).
- Restore is deferred one plugin per loop turn; persist is gated while it runs.
- Event names live once in `WebViewEditor.h` and once in `bridge.js`; they must match.
- A song is a plan, not a timeline: songs/sections/chords change no audio path,
  no capture geometry and no existing state-format key (`songs` is a new,
  independent properties key; absent = empty). One linked recording per
  section; a deleted snippet clears its link.
- Rehearsal is a click, not a sequencer: it advances `metronomePosition` and
  sounds the click (subject to the master click toggle), never sends MIDI and
  never changes the print, the stems or the capture.
