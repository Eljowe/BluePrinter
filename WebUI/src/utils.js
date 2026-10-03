// Utility helpers used across the UI.

// The 8 organisational colour tags a snippet can carry. `key` is the
// value stored in the backend / sidecar JSON; `main` is the strong
// accent and `soft` a pastel wash for backgrounds.
export const SNIPPET_COLORS = [
  { key: "red",    label: "Red",    main: "#e26d6a", soft: "#f6d3d1" },
  { key: "orange", label: "Orange", main: "#e89b4e", soft: "#f7e0c8" },
  { key: "yellow", label: "Yellow", main: "#d9b93c", soft: "#f7edc4" },
  { key: "green",  label: "Green",  main: "#6bbf6a", soft: "#d7ecd3" },
  { key: "teal",   label: "Teal",   main: "#4db6ac", soft: "#cdeae5" },
  { key: "blue",   label: "Blue",   main: "#5570cf", soft: "#d9e1f7" },
  { key: "purple", label: "Purple", main: "#9575cd", soft: "#e2d9f2" },
  { key: "pink",   label: "Pink",   main: "#e87ea0", soft: "#f7d8e2" },
];

export function snippetColor(key) {
  return SNIPPET_COLORS.find((c) => c.key === key) ?? null;
}

export function formatTime(seconds) {
  if (!Number.isFinite(seconds) || seconds < 0) return "00:00.0";
  const total = Math.max(0, seconds);
  const mm = Math.floor(total / 60);
  const ss = Math.floor(total % 60);
  const tenths = Math.floor((total * 10) % 10);
  return `${String(mm).padStart(2, "0")}:${String(ss).padStart(2, "0")}.${tenths}`;
}

export function formatDate(iso) {
  if (!iso) return "";
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) return "";
  const yyyy = d.getFullYear();
  const mm = String(d.getMonth() + 1).padStart(2, "0");
  const dd = String(d.getDate()).padStart(2, "0");
  const hh = String(d.getHours()).padStart(2, "0");
  const min = String(d.getMinutes()).padStart(2, "0");
  return `${yyyy}-${mm}-${dd} ${hh}:${min}`;
}

export function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

export function debounce(fn, wait) {
  let timer = null;
  return (...args) => {
    if (timer) clearTimeout(timer);
    timer = setTimeout(() => {
      timer = null;
      fn(...args);
    }, wait);
  };
}

// --- Practice scales (0053) -----------------------------------------------
//
// Pure music theory for the header Scales popover. Pitch classes are 0..11
// with C = 0. Note spelling follows the key signature: a minor-flavoured
// scale borrows its relative major's key (so A minor reads with naturals,
// E minor with F#, D minor with Bb), and each scale degree is assigned its
// conventional letter so the blues flat-five reads Eb in A, not D#.

// `intervals` are semitone offsets from the root; `letters` are the diatonic
// letter offsets of each degree (used to spell accidentals correctly);
// `minor` selects the relative-major key signature and the root name table.
export const SCALES = [
  { id: "major",           label: "Major",            intervals: [0, 2, 4, 5, 7, 9, 11], letters: [0, 1, 2, 3, 4, 5, 6], minor: false },
  { id: "minor",           label: "Natural minor",    intervals: [0, 2, 3, 5, 7, 8, 10], letters: [0, 1, 2, 3, 4, 5, 6], minor: true },
  { id: "majorPentatonic", label: "Major pentatonic", intervals: [0, 2, 4, 7, 9],         letters: [0, 1, 2, 4, 5],       minor: false },
  { id: "minorPentatonic", label: "Minor pentatonic", intervals: [0, 3, 5, 7, 10],        letters: [0, 2, 3, 4, 6],       minor: true },
  { id: "blues",           label: "Blues",            intervals: [0, 3, 5, 6, 7, 10],     letters: [0, 2, 3, 4, 4, 6],    minor: true },
];

export const SCALE_IDS = SCALES.map((s) => s.id);

// Conventional 12-note root names. Major keys use flats for Db/Eb/Ab/Bb and
// F#; minor keys use C#/F#/G# instead of the theoretical Db/Gb/Ab minors.
const MAJOR_ROOT_NAMES = ["C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"];
const MINOR_ROOT_NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B"];

const LETTERS = ["C", "D", "E", "F", "G", "A", "B"];
const NATURAL_PC = [0, 2, 4, 5, 7, 9, 11];
const LETTER_INDEX = { C: 0, D: 1, E: 2, F: 3, G: 4, A: 5, B: 6 };

// Major keys conventionally written with flats.
const FLAT_MAJOR_PCS = new Set([1, 3, 5, 8, 10]);

function mod12(value) {
  const n = Math.round(Number(value));
  return ((Number.isFinite(n) ? n : 0) % 12 + 12) % 12;
}

export function scaleById(id) {
  return SCALES.find((s) => s.id === id) ?? SCALES[0];
}

// The 12 root labels for the selector, spelled for the scale's mode.
export function rootNames(scaleId) {
  return scaleById(scaleId).minor ? MINOR_ROOT_NAMES : MAJOR_ROOT_NAMES;
}

export function rootName(pc, scaleId) {
  return rootNames(scaleId)[mod12(pc)];
}

// The major key whose key signature a root+scale is written in: a
// minor-flavoured scale borrows its relative major (root + 3 semitones).
export function spellingMajorPc(rootPc, scaleId) {
  const root = mod12(rootPc);
  return scaleById(scaleId).minor ? (root + 3) % 12 : root;
}

export function prefersFlats(rootPc, scaleId) {
  return FLAT_MAJOR_PCS.has(spellingMajorPc(rootPc, scaleId));
}

function spellLetter(letterIndex, pc) {
  const natural = NATURAL_PC[letterIndex];
  const diff = ((pc - natural) % 12 + 12) % 12;
  const accidental = diff === 0 ? "" : diff === 1 ? "#" : diff === 11 ? "b" : diff === 2 ? "##" : diff === 10 ? "bb" : "";
  return LETTERS[letterIndex] + accidental;
}

// The scale as ascending { pc, name, degree } objects from the root.
export function scaleNotes(rootPc, scaleId) {
  const scale = scaleById(scaleId);
  const root = mod12(rootPc);
  const rootLetter = LETTER_INDEX[rootName(root, scale.id)[0]];
  return scale.intervals.map((interval, index) => {
    const pc = (root + interval) % 12;
    const letter = (rootLetter + scale.letters[index]) % 7;
    return { pc, name: spellLetter(letter, pc), degree: interval };
  });
}

export function scalePitchClasses(rootPc, scaleId) {
  return new Set(scaleNotes(rootPc, scaleId).map((note) => note.pc));
}

// Simple pitch-class name for a chromatic note, using the key's accidental
// preference (sharps for sharp keys, flats for flat keys). Used for notes
// outside the scale and for live tuner readings.
export function spellPitchClass(pc, flats) {
  const sharp = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
  const flat = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"];
  return (flats ? flat : sharp)[mod12(pc)];
}

// "A" / "F#" / "Bb" -> pitch class, or -1 when it isn't a note.
export function pitchClassFromName(name) {
  if (typeof name !== "string") return -1;
  const match = name.trim().match(/^([A-Ga-g])([#b]?)/);
  if (!match) return -1;
  const letter = LETTER_INDEX[match[1].toUpperCase()];
  if (letter === undefined) return -1;
  const accidental = match[2] === "#" ? 1 : match[2] === "b" ? -1 : 0;
  return ((NATURAL_PC[letter] + accidental) % 12 + 12) % 12;
}

// Parse a detected key like "A minor" / "F# major" into { pc, scaleId }.
export function parseDetectedKey(key) {
  if (typeof key !== "string") return null;
  const match = key.trim().match(/^([A-Ga-g][#b]?)\s+(major|minor)$/);
  if (!match) return null;
  const pc = pitchClassFromName(match[1]);
  if (pc < 0) return null;
  return { pc, scaleId: match[2].toLowerCase() === "minor" ? "minor" : "major" };
}

// Parse the tuner's note string ("A4", "C#3") into { pc, octave, midi }.
// `octave`/`midi` are null when the string carries no octave.
export function parseNoteName(note) {
  if (typeof note !== "string") return null;
  const match = note.trim().match(/^([A-Ga-g][#b]?)(-?\d+)?$/);
  if (!match) return null;
  const pc = pitchClassFromName(match[1]);
  if (pc < 0) return null;
  if (match[2] === undefined) return { pc, octave: null, midi: null };
  const octave = Number(match[2]);
  return { pc, octave, midi: (octave + 1) * 12 + pc };
}

// Standard tuning, low-E to high-E (MIDI note numbers).
export const STANDARD_TUNING = [
  { midi: 40, label: "E" },
  { midi: 45, label: "A" },
  { midi: 50, label: "D" },
  { midi: 55, label: "G" },
  { midi: 59, label: "B" },
  { midi: 64, label: "E" },
];

// --- Scales popover prefill state machine (0053) --------------------------
//
// The popover prefills its key from the currently playing snippet, but the
// playing id can arrive before the snippet payload that carries its detected
// key, and the user can override the selectors at any time. This pure step
// keeps that honest: a source whose key is still pending stays unlocked and is
// retried on the next push, while a source that has already prefilled (or been
// edited by hand) is never overwritten. A new playing source resets the guard.
// `state` is `{ source, locked }`; the return is `{ state, apply }`, where
// `apply` is the parsed `{ pc, scaleId }` to adopt or null to leave the
// selection alone.
export function scalePrefillStep(state, sourceId, parsedKey) {
  const id = Number(sourceId);
  const next = id === state.source ? state : { source: id, locked: false };
  if (id < 0 || next.locked || !parsedKey) return { state: next, apply: null };
  return { state: { source: id, locked: true }, apply: parsedKey };
}

// Mark the current source as claimed by a manual selector change, so a key
// that arrives later (or a re-detected key) can never clobber it.
export function lockScalePrefill(state) {
  return { ...state, locked: true };
}

// Reconcile the frontend tool owner with the authoritative backend
// `tunerOpen` flag (0053): true keeps the current Tuner/Scales owner (or
// defaults to the Tuner when none), false clears it. Returning `prev`
// unchanged when nothing needs to move lets the App effect key off
// `tunerOpen` alone without re-emitting or looping at the 30 Hz transport
// cadence. Only ever returns "tuner" / "scales" / null.
export function reconcileActiveTool(prev, tunerOpen) {
  if (tunerOpen) return prev ?? "tuner";
  return prev === null ? prev : null;
}
