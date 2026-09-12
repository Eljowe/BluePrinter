---
id: "0032"
title: "Time signatures beyond 4/4"
status: ready-for-agent
blocked_by: []
---

# Time signatures beyond 4/4

## Problem

The looper grid assumes every bar is four beats: `LooperGrid` hardcodes `beat * 4`,
and the metronome's bar grouping is fed `countInBeats` rather than a real
beats-per-bar. Songs in 3/4, 6/8, 5/4 or 7/8 cannot be captured on the grid, which
makes the tempo-locked looper and click unusable for them.

## Current behaviour

`LooperGrid::computeLength` / `computeFixedLengthSamples` use `bar = beat * 4`, and
`computeCrop` derives beats from the quarter-note period. `MetronomePlayer` accepts a
`beatsPerBar`, but the processor passes `countInBeats` into that slot, so accents only
group by accident (count-in defaults to 4) and group per-beat when count-in is 0.
Count-in (`PreRoll::isComplete`) counts beats and is already meter-agnostic; MIDI clock
is quarter-note PPQN and is unaffected by the meter.

## Decisions (grilled)

- **BPM reference:** quarter note (the MIDI/DAW convention). The notated **beat is the
  denominator** note, so `samplesPerBeat = 60/bpm · sampleRate · (4 / denominator)` and
  `beatsPerBar = numerator`. In 6/8 at 120, the eighth is the beat and a bar is six
  eighths.
- **Meter set:** fixed dropdown — 2/4, 3/4, 4/4, 5/4, 6/8, 7/8, 9/8, 12/8. Custom
  meters are a later ticket.
- **Accents:** beat 1 only. Compound secondary accents (e.g. beat 4 in 6/8) are out of
  scope.
- **Existing loops:** switching meter never re-times or clears loop audio. The existing
  loop's beat count (ruler + crop limits) recomputes at the current meter; the meter
  applies to future captures and the click.
- **UI:** meter selector in the sync strip beside BPM; persists in standalone user state
  (default 4/4) — **not** an APVTS parameter.
- **Non-negotiables:** fixed-length capture and trim-to-grid use the meter; the
  metronome is driven by a **real beats-per-bar** (fixing the count-in miswire);
  `LooperGrid` math and the ruler labels/beat grouping become meter-aware; count-in
  stays beat-based; MIDI clock/Start behaviour is unchanged.

## Agent Brief

**Category:** enhancement
**Summary:** Make the click, fixed-length capture, grid trim and looper ruler respect a
selectable time signature, with BPM anchored to the quarter note.

**Current behavior:**
Bars are hardcoded to four beats in the looper grid math, the metronome's accented bar
grouping is fed the count-in value instead of a beats-per-bar, and the ruler labels
assume 4/4. There is no meter setting.

**Desired behavior:**
- A time-signature setting (numerator + denominator, chosen from the fixed list above)
  is persisted with user state, defaults to 4/4, and is exposed in the sync strip.
- BPM stays quarter-note referenced. The notated beat is the denominator note, so
  `samplesPerBeat = 60/bpm · sampleRate · 4/denominator`; a bar is `numerator` beats.
- Fixed-length capture records exactly N bars at the selected meter; trim-to-grid snaps
  to the meter's bar length; crop limits and ruler labels/beat grouping use the meter.
- The metronome accents the first beat of each meter-sized bar (driven by the real
  numerator, not the count-in value). Count-in continues to count beats (denominator
  notes).
- Changing the meter re-interprets an existing loop's beat count from the current meter
  but never alters, re-times or clears its audio.
- MIDI clock pulse and Start behaviour are unchanged. 4/4 remains the default and is
  behaviourally identical to today.

**Key interfaces:**
- Introduce a time-signature value (numerator, denominator) as standalone user state
  readable on the audio thread; the processor supplies it wherever the current constant
  4 is used.
- `LooperGrid` length / fixed-length / crop functions take `beatsPerBar` and the
  denominator (or a meter struct) instead of assuming 4 quarter-note beats per bar.
- `MetronomePlayer` receives the real beats-per-bar; the mistyped count-in argument is
  removed.
- The transport snapshot and the ruler carry the meter; a new frontend→backend event
  sets it, registered in the shared event constants and mirrored in the WebUI bridge
  (parity test must pass).

**Acceptance criteria:**
- [ ] Fixed-length capture records exactly N bars in every listed meter, verified over
      repeated cycles with the click.
- [ ] Trim-to-grid snaps a free capture to whole bars in the selected meter.
- [ ] The click accents beat 1 of a meter-sized bar for every meter, including with
      count-in set to 0 and numerators other than 4 (the previous behaviour is gone).
- [ ] The ruler labels and beat grouping reflect the selected meter.
- [ ] Changing the meter leaves existing loop audio byte-for-byte unchanged.
- [ ] The meter persists across restart; 4/4 default matches today's behaviour exactly.
- [ ] MIDI clock pulses and Start timing are unchanged.

## Docs

`AGENTS.md` (BPM/looper/MIDI clock), `README.md`, `juce-audio` and `webui-bridge`
skill notes.

## Files

`Source/LooperGridMath.h/.cpp`, `Source/MetronomePlayer.h/.cpp`,
`Source/PluginProcessor.h/.cpp`, `Source/WebViewEditor.h/.cpp`,
`WebUI/src/components/Looper.jsx`, `WebUI/src/components/SyncControls.jsx`,
`WebUI/src/bridge.js`, `Tests/`.

## Out of scope

Custom numerator/denominator entry; compound-meter secondary accents; tempo maps;
polymeter; host tempo/signature sync.
