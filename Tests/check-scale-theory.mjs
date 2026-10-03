#!/usr/bin/env node
// Regression check for the pure practice-scale theory (ticket 0053).
//
// The helpers live in WebUI/src/utils.js (which is plain ESM, no DOM), so
// this script imports them directly and asserts the conventional note
// spellings and parsers. Run directly (`node Tests/check-scale-theory.mjs`)
// or via CTest (`ScaleTheory`).

import {
  SCALES,
  SCALE_IDS,
  scaleNotes,
  rootName,
  parseDetectedKey,
  parseNoteName,
  pitchClassFromName,
  scalePrefillStep,
  lockScalePrefill,
  reconcileActiveTool,
} from "../WebUI/src/utils.js";

let failures = 0;

function check(cond, message) {
  if (!cond) {
    failures += 1;
    console.error(`FAIL ${message}`);
  }
}

function notesOf(root, scaleId) {
  return scaleNotes(root, scaleId).map((note) => note.name).join(" ");
}

// Conventional spellings (sharps for sharp keys, flats for flat keys,
// minor keys borrowing their relative major's signature).
check(notesOf(0, "major") === "C D E F G A B", "C major");
check(notesOf(9, "minor") === "A B C D E F G", "A natural minor");
check(notesOf(5, "major") === "F G A Bb C D E", "F major (flats)");
check(notesOf(2, "major") === "D E F# G A B C#", "D major (sharps)");
check(notesOf(4, "minor") === "E F# G A B C D", "E natural minor (F#)");
check(notesOf(1, "minor") === "C# D# E F# G# A B", "C# natural minor");
check(notesOf(9, "majorPentatonic") === "A B C# E F#", "A major pentatonic");
check(notesOf(9, "minorPentatonic") === "A C D E G", "A minor pentatonic");
check(notesOf(9, "blues") === "A C D Eb E G", "A blues (flat five)");
check(notesOf(6, "majorPentatonic") === "F# G# A# C# D#", "F# major pentatonic");
check(notesOf(4, "blues") === "E G A Bb B D", "E blues (flat five)");

// All five scales render a full, unique, root-anchored set for all 12 roots.
for (const scale of SCALES) {
  for (let root = 0; root < 12; root += 1) {
    const notes = scaleNotes(root, scale.id);
    check(notes.length === scale.intervals.length, `${scale.id} root ${root}: note count`);
    check(notes[0].pc === root, `${scale.id} root ${root}: starts on the root`);
    check(new Set(notes.map((n) => n.pc)).size === notes.length, `${scale.id} root ${root}: unique pcs`);
    check(notes.every((n) => /^[A-G](b|#|##|bb)?$/.test(n.name)), `${scale.id} root ${root}: names spelled`);
    check(rootName(root, scale.id).length > 0, `${scale.id} root ${root}: root label`);
  }
}

check(SCALE_IDS.length === 5, "five scale ids");

// Detected-key parsing (KeyDetector emits sharps + " major"/" minor").
const aMinor = parseDetectedKey("A minor");
check(aMinor && aMinor.pc === 9 && aMinor.scaleId === "minor", "parse A minor");
const fsMajor = parseDetectedKey("F# major");
check(fsMajor && fsMajor.pc === 6 && fsMajor.scaleId === "major", "parse F# major");
check(parseDetectedKey("") === null, "parse empty key");
check(parseDetectedKey("nonsense") === null, "parse bad key");

// Tuner note parsing (name + octave, exact MIDI).
const a4 = parseNoteName("A4");
check(a4 && a4.pc === 9 && a4.octave === 4 && a4.midi === 69, "parse A4");
const cs3 = parseNoteName("C#3");
check(cs3 && cs3.pc === 1 && cs3.midi === 49, "parse C#3");
const bareB = parseNoteName("B");
check(bareB && bareB.pc === 11 && bareB.midi === null, "parse bare B");
check(pitchClassFromName("Bb") === 10, "pitch class Bb");
check(pitchClassFromName("H") === -1, "reject bad note letter");

// --- Scales popover prefill state machine (0053) -------------------------
//
// The bug this guards: the playing id can land before the snippet payload
// that carries its detected key. The old code marked the source as seen
// before parsing, so a key that arrived a push later was ignored. The pure
// step must retry a keyless source, never overwrite a manual choice made
// while the key was pending, preserve manual edits on later pushes of the
// same source, and reset on a new source.
const freshPrefill = { source: undefined, locked: false };

// Late key: the source is tracked but not applied until the key arrives.
let late = scalePrefillStep(freshPrefill, 7, null);
check(late.apply === null, "late key: no prefill while the key is pending");
check(late.state.source === 7 && late.state.locked === false, "late key: source tracked, unlocked");
late = scalePrefillStep(late.state, 7, parseDetectedKey("A minor"));
check(late.apply && late.apply.pc === 9 && late.apply.scaleId === "minor", "late key: prefills once it arrives");
check(late.state.locked === true, "late key: locks after prefill");
late = scalePrefillStep(late.state, 7, parseDetectedKey("C major"));
check(late.apply === null, "late key: later same-source key does not overwrite");

// Manual override while the key is pending: the late key must not clobber it.
let manual = scalePrefillStep(freshPrefill, 8, null);
manual = { state: lockScalePrefill(manual.state), apply: manual.apply };
check(manual.state.locked === true, "manual before late key: source locked");
manual = scalePrefillStep(manual.state, 8, parseDetectedKey("D major"));
check(manual.apply === null, "manual before late key: late key ignored");

// Manual edits are preserved across later pushes of the same source.
let edited = scalePrefillStep(freshPrefill, 9, parseDetectedKey("G major"));
check(edited.apply && edited.apply.pc === 7, "same source: prefills from the key");
edited = { state: lockScalePrefill(edited.state), apply: edited.apply };
edited = scalePrefillStep(edited.state, 9, parseDetectedKey("E minor"));
check(edited.apply === null, "same source: manual edits preserved");

// A new playing source resets the guard and prefills again.
const nextSource = scalePrefillStep(edited.state, 10, parseDetectedKey("F major"));
check(nextSource.apply && nextSource.apply.pc === 5, "new source: prefills again");
check(nextSource.state.source === 10 && nextSource.state.locked === true, "new source: locks");

// Playback stopping (-1) resets, so a later restart prefills.
const stopped = scalePrefillStep(nextSource.state, -1, null);
check(stopped.apply === null && stopped.state.locked === false, "stopped source: resets the guard");

// Tool-owner reconciliation (0053): a late backend `tunerOpen` change must
// adopt an open analysis and drop a closed one, and must return the previous
// owner unchanged when nothing moves (so the App effect can't loop).
check(reconcileActiveTool(null, true) === "tuner", "restore open: defaults to Tuner");
check(reconcileActiveTool("scales", true) === "scales", "restore open: retains Scales");
check(reconcileActiveTool("tuner", true) === "tuner", "restore open: retains Tuner");
check(reconcileActiveTool("scales", false) === null, "restore close: clears the owner");
check(reconcileActiveTool(null, false) === null, "restore close: already closed stays closed");

if (failures === 0) {
  console.log("ok   ScaleTheory: all scale/parsing checks passed");
  process.exit(0);
}

console.error(`FAIL ScaleTheory: ${failures} check(s) failed`);
process.exit(1);
