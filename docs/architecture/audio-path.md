# Audio path: chains, capture, playback and timing

Moved verbatim from `AGENTS.md` (ticket 0064, 2026-11-21). Rules stay in
[`AGENTS.md`](../../AGENTS.md); this file keeps the mechanics and incident history. The routing
invariants and the recording-mix definition are stated in AGENTS.md; this doc holds the full
mechanics.

## Chain model

- Each chain has a **stable id** (`"chain0"`, `"chain1"`, … generated from `nextChainId`,
  persisted), a **user-editable name**, an **input mask** (any subset of the 1–8 channel input bus,
  `ChainInputBits`), a **MIDI toggle** (`wantsMidi`), a **MIDI channel filter** (`midiChannelsMask`,
  channels 1–16; system messages always pass), a **Record toggle** (`recordOnCapture`), a **volume**
  (dB, −60..+12, default 0), a **mute** toggle, two **monitor-only** flags
  (`monitorSolo`/`monitorMuted` — change what is heard, never the capture), and an **output level
  meter** (`outputLevel`/`outputPeak` atomics, public, written per block post-volume, pushed at 30
  Hz via `chainLevels` in the transport snapshot). **Per-chain cost readout** (0047): `processBlock`
  times each chain's `processBlock` with a monotonic clock, normalises the elapsed time against the
  block budget (`numSamples / sampleRate`) and folds it into `cpuFraction` via `ChainCpuMath` (pure,
  unit-tested); `PluginChain::getActiveLatencySamples()` sums `getLatencySamples()` over loaded,
  non-bypassed slots (the accessor 0046 reuses). `chainLevels` entries carry `cpu`, `latencyMs` and
  `latencySamples`, and `PluginChain.jsx` shows a compact labelled CPU %/latency row (dashed when no
  plugin is active; inactive chains reset to 0, so no stale stats). No new event; the latency
  readout reuses the 0046 accessor. New chains default to: inputs both, wantsMidi on, all MIDI
  channels, record on, 0 dB, unmuted, not soloed/monitor-muted.

## Routing: the standalone MIDI input limitation

The routing rule itself (pristine input, no chain hears another, transparent chains skipped) is in
AGENTS.md. The MIDI input limitation is:

- **The standalone merges all MIDI input devices into one stream** (`AudioProcessorPlayer` discards
  the `MidiInput*`), JUCE 8 has no per-message source ID, and `MidiMessage` carries no device field
  — so the MIDI channel is the only discriminator the plugin can see. Per-device routing would
  require a custom standalone app (`JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP`); per-chain device
  filters don't exist by design. Channels beyond the block's count (e.g. "Right" in mono) receive
  silence.

## Plugin delay compensation (0046)

- Per-chain latency = Σ `getLatencySamples()` over loaded, **non-bypassed** slots
  (`PluginChain::refreshActiveLatencySamples()` takes the slot lock under `tryEnter()` and falls
  back to `activeLatencySamples`, which `PluginChain::processBlock` caches from the snapshot it
  actually runs; `getActiveLatencySamples()` stays the message-thread/UI read). `L` = max over
  loaded, non-bypassed, **non-hard-muted** chains (monitor solo/mute never change it), capped at 500
  ms at the session rate; a source past the cap is clamped and one notification is raised
  (`latencyCapWarningPending`/`Issued`, consumed by `WebViewEditor::timerCallback` →
  `sendNotification(..., "warning")`; no new bridge event). `DelayCompensation::computeInto` (pure,
  `Source/DelayCompensation.h`, CTest) fills the preallocated per-chain delays = `L - lat_i`; the
  dry path is delayed by `L` (`dryDelay`) and each chain by its own delay
  (`PluginChain::outputDelay` via `applyOutputDelay`) **after** its pre-delay post-volume meter and
  **before** the sum. `recordingMixBuffer` is built from the **delayed** dry (`buffer`, Dry already
  applied), not `chainInputBuffer`; the dry stem (index 0) uses the same delayed dry with gain 1.0.
  All delay lines are preallocated on the message thread (`prepareToPlay` + the `createChain`
  prepare hook) and `processBlock` only reads/writes them (no allocation/lock); a delay change
  clears the affected line. `L` is **latched** while a take/loop capture is active (count-in
  included) and `effectiveLatencySamples` is reported to the host by `setLatencySamples` **only when
  it changes and only from `timerCallback`** (never the audio thread). **Capture cursor offset**:
  the print is delayed by `L`, so the capture skips the first `L` frames
  (`CaptureWrite::write`/`TakeRecorder::write`/`Looper::captureBlock`/`StemCapture::writeStem` take
  a `sourceOffset`); the skip counters are set when actual capture starts (and at the capture-active
  transition), consumed only by blocks that write, and reset on stop/cancel/auto-stop/clear.
  Fixed-length auto-stop counts written samples (exactly N bars), and stems share the same offset
  and aligned sources. The click, loop playback, take-overdub monitor, snippet/take-review playback
  and the direct MIDI clock stay **undelayed** on the true grid; the input meter/tuner keep tapping
  `chainInputBuffer`. `L == 0` is a bit-identical no-op.

## Levels and session-only settings

- The looper's **Loop** level is a standalone atomic (`loopLevel`, dB) like the metronome settings.
  Loop playback also has session-only **Reverse** / **Half-speed** atomics
  (`loopPlaybackReverse`/`loopPlaybackHalfSpeed`, 0036): `LoopPlayback::renderMode` holds a
  forward-advancing phase that wraps, maps reverse to a mirrored read, and plays half-speed
  tape-style (0.5×) with linear interpolation across the loop seam; overdub forces forward 1×.
  Metronome/BPM/count-in/click-sound/MIDI-clock/MIDI-device settings are standalone user state
  (`juce::PropertiesFile`) + atomics, **not** APVTS parameters.

## Recording, loop trimming and fixed-length capture

- A captured loop is trimmed on stop to the nearest whole bar (`trimLooperToMusicalGrid`): the
  window length is snapped to that target (a short capture is zero-padded, audio past it truncated)
  instead of clamped back to the raw capture, so the loop wraps exactly on the beat grid — a
  non-grid length made every cycle's downbeat drift, heard as the second loop starting late. All
  chains on = identical to the old post-chain tap.
- **Fixed-length capture** (0022): the looper's **Length** selector (`looperLengthBars`: 0 = Free,
  else 1/2/4/8/12/16 bars) auto-stops a *fresh* capture once that many bars (the meter's numerator
  beats each, `timeSignatureNumerator`/`timeSignatureDenominator`, default 4/4) have been written.
  BPM stays a quarter-note tempo, so an eighth beat (6/8) is half a quarter; the meter is read once
  per block and passed to `LooperGrid` (length/fixed-length/crop), `MetronomePlayer::setContext`
  (takes `beatUnit`, the subdivision count and the per-beat accent mask — 0050) and
  `PreRoll::isComplete` (so the count-in counts denominator beats). `frontendSetTimeSignature { numerator, denominator }`
  sets it, it persists in the host state (`timeSignatureNumerator`/`timeSignatureDenominator`) and
  ships in `backendTransport`. Changing the meter never re-times existing loop audio. The audio
  thread compares `audioLoopLength` to the target (`LooperGrid::computeFixedLengthSamples`) and, at
  the boundary, clears `looperCaptureArmed`/`audioLoopRecording` and sets `looperAutoStopPending`;
  `timerCallback` then calls `setLooperRecording(false)`, so the trim/mix/listeners stay on the
  message thread. Overdub layers are never auto-stopped (the loop already defines the length).
  `looperLengthBars` persists in the host state.

## Take stack and take overdub (0023/0037)

- Takes are **not** saved automatically (0037): every stopped take is appended to a bounded stack (8
  takes / 256 MB, oldest dropped with a toast) in `Source/TakeRecorder.h/.cpp`, each owning its own
  audio buffer — `recordBuffer` is only the live capture scratch now, so a new take (or a loop
  capture) no longer invalidates the stack. `TakeReview` shows a chip row; one take is selected
  (newest auto-selected; deleting selects the neighbour) and auditioned via
  `TakeRecorder::renderReview` (one-shot, overwrites output, scaled by the master Output), then
  saved (`savePendingTake` removes the saved take: adds a snippet + writes WAV/JSON to the library
  folder when set) or deleted (`discardPendingTake`/`discardTake`, per chip, or `discardAllTakes`).
  `frontendSetTakePlayback`/`frontendSaveTake { id }`/`frontendDiscardTake { id }`/`frontendSelectTake { id }`/`frontendDiscardAllTakes`
  drive it; `backendTransport` ships `takes`/`selectedTakeId` plus the selected-take
  `takeLength`/`takePlaying`/`takePosition`/`takePeaks`/`takeOverdub`.
- **Take overdub** (0023): with a take selected and the `TakeReview` **Dub** toggle on
  (`frontendSetTakeOverdub`, session-only), `startRecording` layers instead of adding a new take —
  `beginActualTakeOverdub` stages the selected take into `recordBuffer`, arms the layer write at
  `takeLength` and plays the take from position 0 (monitor-only, looping, silent through a count-in
  so the layer aligns); the record tap writes the layer (`TakeRecorder::write`), and
  `finalizeRecordingOnMessageThread` wrap-mixes `[takeLength, recordWritePos)` into `[0, takeLength)`
  via `mixOverdubLayer` and replaces the selected take's audio in place (same `takeId`). Turning Dub
  off (or no pending take) records a fresh take as before.

## Loop layer undo/redo (0030)

- **Loop layer undo/redo** (0030): before each looper overdub layer is mixed (the
  `looperOverdubCapture` branch of `setLooperRecording(false)`), `pushLoopUndoSnapshot()` copies the
  FULL loop region; the last 10 snapshots (and ~64 MB) are kept, oldest-first.
  `undoLoopLayer`/`redoLoopLayer` restore a snapshot, refresh the waveform peaks and update the loop
  clip latch (`updateLoopClipLatch`). Session-only; a fresh capture (`trimLooperToMusicalGrid`) or
  `clearLoop()` clears the history; undo/redo are refused while playing/capturing. Events
  `frontendLoopUndo`/`frontendLoopRedo`; the transport snapshot ships
  `loopUndoAvailable`/`loopRedoAvailable`.

## Save the loop / load a snippet into the looper (0025/0056)

- The looper's **Save to library** already materialises the cropped loop as a normal snippet (0025's
  "bounce loop").
- **Load into looper** (0056): `frontendLoadSnippetIntoLooper { id }` resamples a library snippet to
  the session rate (`Source/Resampler.h/.cpp`, unit-tested: Lagrange conversion + channel mapping)
  and publishes it as the current loop via `Looper::setLoaded` (start 0 / length = full, crop and
  layer history cleared); the snippet is never modified, playback is stopped first, and the load is
  refused during a capture and above half the record buffer (~60 s). Saving the overdubbed loop
  makes a new snippet named `<source> overdub` (the original stays), and the transport snapshot
  ships `looperSourceName` for the looper's "Loaded:" chip (cleared on Discard or a fresh capture).

## Per-chain capture stems (0038)

- **Stem export** (0038): a session-only `frontendSetCaptureStems { enabled }` toggle (sync strip)
  arms `StemCapture` (`Source/StemCapture.h`) for the next **fresh** take or loop capture; the audio
  thread taps the dry pass-through and each `recordOnCapture`, non-muted chain's post-volume output
  into preallocated, frame-aligned buffers — the stem write position mirrors the capture module's
  cursor (`TakeRecorder::getWritePos` / `Looper::getLength`), so a grid-trimmed loop truncates/pads
  its stems identically, and overdub layers are never stemmed. `frontendExportStems { source }`
  (`"take"`/`"loop"`) opens the same WAV/AIFF/FLAC chooser and writes `<base>-<stem><ext>` per stem
  (dry + chains, names sanitised/de-duplicated; no `gainDb` bake) via `exportSnippetToFile`.
- Stems follow the **capture bus only** — monitor solo/mute and the master Output never change them
  — so they sum back to the captured mix. Session-only, latest capture only, ~256 MB cap; the
  transport snapshot ships `captureStemsEnabled`/`stemsAvailable`/`stemSource`, and the
  TakeReview/Looper show an **Export stems** button when their capture has stems.

## Metering, clip latches and the monitor guard (0048)

- **Metering / clip latches**: `processBlock` drives four `Meter` objects (`Source/Meter.h`) —
  `inputMeter` (post-gain, pre-loop/click clean signal), `recordMeter` (from `recordingMixBuffer`,
  the actual print — unaffected by Output), `outputMeter` (post-master-Output monitor), and
  `loopPlayMeter` (loop playback, set with `setPeak` since it tracks the raw block peak rather than
  an RMS). Each `Meter` owns its level/peak/clip atomics and the smoothed-RMS + peak-hold + latch
  math (`MeterMath::measure`/`smoothLevel`, shared with the per-chain meters via the static
  `Meter::computeInto`); a peak reaching 1.0 latches the clip indicator. All peaks decay at 30 Hz in
  `timerCallback` (`decayPeak`), and the loop level (only written while playing) falls there too.
  `frontendResetClip { target }` (`"input"|"record"|"output"|"loop"|"all"`) clears a latch.
- The monitor deck shows IN/REC/OUT meters (`LevelMeter.jsx`, dBFS −60..0 with a headroom zone,
  peak-hold marker and click-to-reset clip LED); the looper's Loop playback meter sits directly
  under its Loop knob in a settings strip grouped into levels / count-in / mode / crop clusters
  (`.looper-setting`, hairline dividers), with the knobs top-aligned against the other controls.
- The monitor deck also has a **Guard** toggle (0048) with a ceiling knob in a small popover (−12..0
  dBFS, default −1.0, step 0.5): when on, the monitor buffer is soft-clipped (`Source/SoftClip.h`,
  per channel, `ceiling * tanh(x / ceiling)`) after the master Output gain and the tuner monitor
  mute and immediately before `outputMeter.compute`, so the OUT meter reads the guarded signal and a
  pre-clip magnitude past the ceiling latches the OUT clip LED (cleared as usual by
  `frontendResetClip { target: "output" }`). It is monitor-only — `recordingMixBuffer`, the record
  meter and the print are untouched — zero-latency and allocation-free, off by default, and shipped
  in the transport snapshot as `clipGuardEnabled`/`clipGuardCeiling` (persisted in host state as
  `clipGuardEnabled`/`clipGuardCeilingDb`).

## Built-in tuner: the analysis worker (0034)

- **Built-in tuner** (0034): a header **Tuner** popover (`Tuner.jsx`) fed from the clean post-gain
  `chainInputBuffer` (pre-chain/loop/click) — `processBlock` mono-sums it into a preallocated
  single-producer ring (`tunerRing`/`tunerRingWrite`) only while `tunerOpen`. A nested `TunerWorker`
  (`juce::Thread`, started/stopped by `setTunerOpen`) reads the latest 4096 samples and runs
  `Tuner::detectPitch` (YIN cumulative-mean-normalised difference + parabolic interpolation;
  `Source/Tuner.h`, unit-tested) off the audio thread, publishing
  `tunerFrequency`/`tunerConfidence`. `Tuner::describePitch` maps frequency→note+cents against
  `tunerReferencePitch` (persisted, default 440). `setTunerMonitorMute` clears the monitor output at
  the end of `processBlock` (after `recordingMixBuffer` is written), so it never changes the
  capture. Events
  `frontendSetTunerOpen`/`frontendSetTunerReferencePitch`/`frontendSetTunerMonitorMute`;
  `tunerOpen`/`tunerReferencePitch` persist in host state, `tunerMonitorMute` is session-only.

## Take melody extraction and audition (0054)

- **Take melody extraction** (0054): offline, monophonic, takes-only. `MelodyAnalyzer`
  (`Source/MelodyAnalyzer.h/.cpp`, unit-tested) frames the take, runs `Tuner::detectPitch` per
  overlapping window, gates unvoiced frames on confidence, median-filters the pitch track, merges
  legato gaps and drops sub-minimum blips into `NoteEvent { startSample, lengthSamples, midi, cents, confidence }`,
  delegating key/notes to `KeyDetector::detectKey`. `PluginProcessor::analyzeTakeMelody` runs it on
  a detached worker over the selected take's audio (`takeRecorder.getSelectedTakeAudio()`) and
  caches the result per take id in `takeMelodies` (session-only, message thread); a newer analysis
  supersedes an in-flight one.
- `MelodyPlayer` (`Source/MelodyPlayer.h/.cpp`, unit-tested) auditions the notes with a preallocated
  sine+ADSR, rendered additively in `processBlock` (step 7c, after take-review playback) —
  monitor-only, never in `recordingMixBuffer`; `startRecording` and the other playback entry points
  stop it, and `setMelodyPlayback` refuses during a capture.
- Events `frontendAnalyzeTakeMelody { id }` / `frontendSetMelodyPlayback { enabled, startSample? }`
  (a `startSample` auditions from that note; absent/-1 plays from the top); the 30 Hz transport
  snapshot ships the selected take's
  `melodyNotes`/`melodyKey`/`melodyAnalysing`/`melodyPlaying`/`melodyPosition`/`melodyLength`.
  Saving a take copies the cached notes into `Snippet::melody` and the sidecar (`melody` array); the
  card shows a note count. `PianoRoll.jsx` + the `TakeReview` melody panel render it: the roll is
  interactive — wheel zooms about the cursor, shift-wheel pans, double-click / the −·Fit·+ buttons
  reset, a time ruler and a beat grid (from `bpm` + the meter's numerator) plus a pitch-name gutter
  frame it, and a **Copy notes** button puts the key + note list on the clipboard. Hovering or
  focusing a note shows `name · start · length · cents`; arrow keys / Home / End move between notes
  and Enter auditions from the focused one. No `.mid` export (0026 stays wontfix); complements 0053
  (live scale/fretboard).
- **Library snippets (0055)**: `snippetToVar` also ships the `melody` note array + a
  `melodyAnalysing` flag, and the transport snapshot ships `melodyPlayingSource`/`melodyPlayingId`.
  `analyzeSnippetMelody(id)` mirrors `detectSnippetKeyAndNotes` (strong-ref the audio, per-id
  generation counter so a newer request supersedes an older worker, detached YIN pass,
  `library.updateMelody` + `persistMetadata`, `libraryChanged`); the analyser's key is copied in
  only when the snippet has none. `setSnippetMelodyPlayback(id, enabled, startSample?)` reuses
  `MelodyPlayer` with a `MelodySource { none, take, snippet }` guard so the take review and the card
  each show the right play/stop state. `SnippetCard.jsx` expanded details get a **Melody** section
  (piano-roll + key + **Analyse melody**/**Re-analyse** + **Play melody**/**Stop melody**). Events
  `frontendAnalyzeSnippetMelody { id }` / `frontendSetSnippetMelodyPlayback { id, enabled, startSample? }`.

## MIDI clock, click gating and the free-run block

- **MIDI clock**: one **header-level** toggle (`midiClockEnabled` in the sync strip,
  `SyncControls.jsx`) — when on, the clock **free-runs** (`clockRunning` atomic; if no transport
  step advanced `metronomePosition` in a block, the clock advances it itself and sounds the audible
  click, subject to the metronome toggle), so a drum machine can be driven without recording. The
  **song rehearsal** (0066) rides the same free-run block through its own session-only
  `practiceRunning` flag: the position advances and the click sounds without the MIDI clock, but the
  pulse output stays gated on `clockRunning` so a rehearsal never sends MIDI.
- A second header-level toggle, **Clock: on record** (`midiClockOnRecord`, default off,
  `frontendSetMidiClockOnRecord`), restricts the clock to take/loop captures: it starts when a
  capture begins (count-in pre-roll included) and stops when it ends — idle = stopped and the output
  device closed. The run condition is a single atomics-only helper, `wantsClockRun()` =
  `midiClockEnabled && (! midiClockOnRecord || recordingRequested || preRollActive || looperCaptureArmed || looperPreRollActive)`,
  evaluated by both `updateClockRunState` (message thread, opens/closes the device) and
  `refreshClockRunning` (audio-thread mirror). Takes and loop captures **ride the clock either way**
  — actual-recording/capture time fires Start again (`midiStartPending` +
  `midiClockOutput.sendStart()`) so external gear re-syncs its pattern on the take's first beat.
  Toggling sends Start/Stop **to the output device** because the standalone never forwards the host
  MIDI buffer to hardware; the MIDI out device picker also lives in the sync strip. **The sends
  leave the audio thread**: `MidiClockOutput::sendClock/sendStart/sendStop` only push into a
  lock-free `MidiSendQueue` (`Source/MidiSendQueue.h`, unit-tested) — clock pulses through a
  fixed-size SPSC ring, Start/Stop through a one-slot mailbox so a full ring can never drop a
  transport command — and `MidiClockOutput`'s own sender thread drains it (`sendMessageNow`) — every
  send wakes the thread with `notify()`, so the pulses leave at the block boundary without the audio
  thread touching the driver. The message thread keeps owning `open`/`close`. Both toggles persist
  in state (`midiClockEnabled`/`midiClockOnRecord`, both default off; the launch device re-open
  skips on-record mode).
- **Click gating**: the master click on/off is `metronomeEnabled` (header-level, shared by
  count-ins, takes, loop captures and the free-running clock); the header-level **Click: capture**
  toggle (`clickDuringCapture`, default on, `frontendSetClickDuringCapture`) is shared by the take
  and the looper — when off, the click plays only during the count-in, never through the
  take/capture. Both are persisted in state.

## Bus layout

- **Bus layout**: `isBusesLayoutSupported` accepts 1–8 input and 1–8 output channels with the two
  counts **independent** (chains only route input channels, so the input width alone drives the
  per-chain chips; the output width is the monitor width). **7.1 (8ch) is the declared default** on
  both buses — the standalone's Audio/MIDI Settings derives its max channels from the bus default
  layout size and, because it shows channels as stereo pairs, caps active pairs at half that
  (`AudioDeviceSelectorComponent`), so a narrow default hid extra inputs (e.g. an aggregated ASIO
  device's 3rd+ pair). VST3 forbids a `discreteChannels()` default, hence a named layout. The
  editor's 30 Hz `timerCallback` polls `getTotalNumInputChannels()` and re-emits the chain snapshot
  when it changes, so the per-chain input chips follow a runtime device reconfiguration.
  `recordBuffer`/snippets are channel-agnostic, so multi-channel captures produce multichannel WAVs.
