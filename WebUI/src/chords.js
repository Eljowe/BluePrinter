// Pure songwriting chord theory (ticket 0058).
//
// The chord scratchpad is frontend-only: this module builds the diatonic and
// borrowed chord catalogue for a key, ranks it against the notes the user has
// selected, and recognises common chord shapes. It deliberately reuses the
// scale spelling helpers in utils.js so the two tools agree on note names.

import {
  LETTER_INDEX,
  SCALES,
  mod12,
  prefersFlats,
  rootName,
  scaleById,
  scaleNotes,
  spellLetter,
  spellPitchClass,
} from "./utils.js";

const ROMAN = ["I", "II", "III", "IV", "V", "VI", "VII"];
const SCALE_ORDER = new Map(SCALES.map((scale, index) => [scale.id, index]));
const CHORD_TONES = [0, 2, 4, 6];
const SUS2_LETTERS = [0, 1, 4];
const SUS4_LETTERS = [0, 3, 4];

const QUALITY_INFO = {
  major: { suffix: "", label: "major" },
  minor: { suffix: "m", label: "minor" },
  diminished: { suffix: "dim", label: "diminished" },
  augmented: { suffix: "aug", label: "augmented" },
  sus2: { suffix: "sus2", label: "suspended second" },
  sus4: { suffix: "sus4", label: "suspended fourth" },
  major7: { suffix: "maj7", label: "major seventh" },
  dominant7: { suffix: "7", label: "dominant seventh" },
  minor7: { suffix: "m7", label: "minor seventh" },
  halfDiminished7: { suffix: "m7b5", label: "half-diminished seventh" },
  diminished7: { suffix: "dim7", label: "diminished seventh" },
  minorMajor7: { suffix: "mMaj7", label: "minor-major seventh" },
};

const QUALITY_BY_INTERVALS = new Map([
  ["0,4,7", "major"],
  ["0,3,7", "minor"],
  ["0,3,6", "diminished"],
  ["0,4,8", "augmented"],
  ["0,2,7", "sus2"],
  ["0,5,7", "sus4"],
  ["0,4,7,11", "major7"],
  ["0,4,7,10", "dominant7"],
  ["0,3,7,10", "minor7"],
  ["0,3,6,10", "halfDiminished7"],
  ["0,3,6,9", "diminished7"],
  ["0,3,7,11", "minorMajor7"],
]);

// Common borrowed chords. `offset` is semitones above the key root; the
// seventh intervals keep the conventional quality for the richer version
// (for example II is a dominant seventh as V/V, and iv is a minor seventh).
const BORROWED = {
  major: [
    { offset: 5, triad: [0, 3, 7], seventh: [0, 3, 7, 10], roman: "iv", label: "parallel minor" },
    { offset: 8, triad: [0, 4, 7], seventh: [0, 4, 7, 11], roman: "bVI", label: "parallel minor" },
    { offset: 10, triad: [0, 4, 7], seventh: [0, 4, 7, 10], roman: "bVII", label: "mixolydian" },
    { offset: 2, triad: [0, 4, 7], seventh: [0, 4, 7, 10], roman: "II", label: "secondary dominant (V/V)" },
  ],
  minor: [
    { offset: 5, triad: [0, 4, 7], seventh: [0, 4, 7, 11], roman: "IV", label: "dorian / parallel major" },
    { offset: 7, triad: [0, 4, 7], seventh: [0, 4, 7, 10], roman: "V", label: "harmonic minor" },
    { offset: 0, triad: [0, 4, 7], seventh: [0, 4, 7, 11], roman: "I", label: "Picardy third" },
    { offset: 1, triad: [0, 4, 7], seventh: [0, 4, 7, 11], roman: "bII", label: "Neapolitan" },
  ],
};

const IDENTIFY_TEMPLATES = [
  { quality: "major", intervals: [0, 4, 7], letters: CHORD_TONES },
  { quality: "minor", intervals: [0, 3, 7], letters: CHORD_TONES },
  { quality: "diminished", intervals: [0, 3, 6], letters: CHORD_TONES },
  { quality: "augmented", intervals: [0, 4, 8], letters: CHORD_TONES },
  { quality: "sus2", intervals: [0, 2, 7], letters: SUS2_LETTERS },
  { quality: "sus4", intervals: [0, 5, 7], letters: SUS4_LETTERS },
  { quality: "major7", intervals: [0, 4, 7, 11], letters: CHORD_TONES },
  { quality: "dominant7", intervals: [0, 4, 7, 10], letters: CHORD_TONES },
  { quality: "minor7", intervals: [0, 3, 7, 10], letters: CHORD_TONES },
  { quality: "halfDiminished7", intervals: [0, 3, 6, 10], letters: CHORD_TONES },
  { quality: "diminished7", intervals: [0, 3, 6, 9], letters: CHORD_TONES },
  { quality: "minorMajor7", intervals: [0, 3, 7, 11], letters: CHORD_TONES },
];

function uniquePcs(pcs) {
  return [...new Set((pcs ?? []).map(mod12))];
}

function intervalsFor(pcs, rootPc) {
  return pcs.map((pc) => mod12(pc - rootPc));
}

function qualityFromIntervals(intervals) {
  const key = uniquePcs(intervals).sort((a, b) => a - b).join(",");
  return QUALITY_BY_INTERVALS.get(key) ?? null;
}

function accidentalWeight(name) {
  if (name.includes("##") || name.includes("bb")) return 2;
  if (name.includes("#") || name.includes("b")) return 1;
  return 0;
}

export function parentScaleId(scaleId) {
  return scaleById(scaleId).minor ? "minor" : "major";
}

export function parentKeyLabel(rootPc, scaleId) {
  const parent = parentScaleId(scaleId);
  return `${rootName(rootPc, parent)} ${parent}`;
}

// Reverse lookup for the Find scales mode: every root/scale pair that contains
// the selected pitch classes, ranked by how many selected tones it contains.
// Full fits come first; within them the tighter (smaller) scale wins, then the
// catalogue order, then the root. Fewer than two selected notes is too broad to
// be useful, so it returns an empty list and the UI prompts for more notes.
export function findScaleMatches(pcs) {
  const selected = uniquePcs(pcs);
  if (selected.length < 2) return [];

  const matches = [];
  for (const scaleDef of SCALES) {
    for (let root = 0; root < 12; root += 1) {
      const notes = scaleNotes(root, scaleDef.id);
      const scalePcs = new Set(notes.map((note) => note.pc));
      const matched = selected.filter((pc) => scalePcs.has(pc)).length;
      if (matched === 0) continue;
      const missing = selected.length - matched;
      matches.push({
        id: `${scaleDef.id}:${root}`,
        rootPc: root,
        scaleId: scaleDef.id,
        rootLabel: rootName(root, scaleDef.id),
        scaleLabel: scaleDef.label,
        name: `${rootName(root, scaleDef.id)} ${scaleDef.label}`,
        notes: notes.map((note) => note.name),
        pcs: notes.map((note) => note.pc),
        matched,
        missing,
        selectedCount: selected.length,
        containsAll: missing === 0,
        size: notes.length,
      });
    }
  }

  return matches.sort((a, b) => (
    b.matched - a.matched
    || a.size - b.size
    || (SCALE_ORDER.get(a.scaleId) ?? 99) - (SCALE_ORDER.get(b.scaleId) ?? 99)
    || a.rootPc - b.rootPc
  ));
}

// The chord root spelling: use the key's own spelled note when it is in the
// parent scale, otherwise the conventional chromatic root (Ab, Bb, F#, ...).
function chordRootName(pc, keyRoot, scaleId) {
  const parent = parentScaleId(scaleId);
  const inScale = scaleNotes(keyRoot, parent).find((note) => note.pc === mod12(pc));
  return inScale ? inScale.name : rootName(pc, "major");
}

function spellChordTones(rootNote, rootPc, intervals, letters) {
  const rootLetter = LETTER_INDEX[rootNote[0]?.toUpperCase()];
  if (rootLetter === undefined) return [];
  return intervals.map((interval, index) => {
    const letter = (rootLetter + (letters[index] ?? CHORD_TONES[index])) % 7;
    return spellLetter(letter, mod12(rootPc + interval));
  });
}

function romanFor(degree, quality, sevenths) {
  const base = ROMAN[degree] ?? "?";
  const lower = ["minor", "minor7", "diminished", "halfDiminished7", "diminished7", "minorMajor7"].includes(quality);
  const numeral = lower ? base.toLowerCase() : base;

  if (!sevenths) {
    if (quality === "diminished") return `${numeral}°`;
    if (quality === "augmented") return `${numeral}+`;
    return numeral;
  }

  switch (quality) {
    case "major7": return `${numeral}maj7`;
    case "dominant7": return `${numeral}7`;
    case "minor7": return `${numeral}7`;
    case "halfDiminished7": return `${numeral}ø7`;
    case "diminished7": return `${numeral}°7`;
    case "minorMajor7": return `${numeral}mMaj7`;
    case "minor": return `${numeral}7`;
    case "major": return `${numeral}maj7`;
    case "diminished": return `${numeral}°7`;
    case "augmented": return `${numeral}+7`;
    default: return `${numeral}7`;
  }
}

function makeChord({ rootPc, rootNote, intervals, letters = CHORD_TONES, roman, group, label = "", degree = null, id }) {
  const quality = qualityFromIntervals(intervals);
  if (!quality) return null;
  const info = QUALITY_INFO[quality];
  const pcs = intervals.map((interval) => mod12(rootPc + interval));
  const notes = spellChordTones(rootNote, rootPc, intervals, letters);
  return {
    id,
    roman,
    symbol: `${rootNote}${info.suffix}`,
    name: `${rootNote} ${info.label}`,
    quality,
    rootPc: mod12(rootPc),
    pcs,
    notes,
    group,
    label,
    degree,
  };
}

export function buildChordCatalog(rootPc, scaleId, { sevenths = false } = {}) {
  const root = mod12(rootPc);
  const parent = parentScaleId(scaleId);
  const scale = scaleNotes(root, parent);
  const diatonic = [];

  for (let degree = 0; degree < scale.length; degree += 1) {
    const rootNote = scale[degree];
    const toneNotes = sevenths
      ? [scale[degree], scale[(degree + 2) % 7], scale[(degree + 4) % 7], scale[(degree + 6) % 7]]
      : [scale[degree], scale[(degree + 2) % 7], scale[(degree + 4) % 7]];
    const intervals = intervalsFor(toneNotes.map((note) => note.pc), rootNote.pc);
    const quality = qualityFromIntervals(intervals);
    const chord = makeChord({
      rootPc: rootNote.pc,
      rootNote: rootNote.name,
      intervals,
      roman: romanFor(degree, quality, sevenths),
      group: "diatonic",
      degree,
      id: `diatonic:${degree}:${sevenths ? 7 : 3}`,
    });
    if (chord) diatonic.push(chord);
  }

  const borrowed = BORROWED[parent].map((spec, index) => {
    const chordRoot = mod12(root + spec.offset);
    return makeChord({
      rootPc: chordRoot,
      rootNote: chordRootName(chordRoot, root, scaleId),
      intervals: sevenths ? spec.seventh : spec.triad,
      roman: spec.roman,
      group: "borrowed",
      label: spec.label,
      id: `borrowed:${index}:${sevenths ? 7 : 3}`,
    });
  }).filter(Boolean);

  return [...diatonic, ...borrowed].map((chord, index) => ({ ...chord, catalogIndex: index }));
}

export function rankChords(catalog, selectedPcs) {
  const selected = uniquePcs(selectedPcs);
  const selectedSet = new Set(selected);

  return catalog
    .map((chord) => {
      const chordSet = new Set(chord.pcs);
      const matched = selected.filter((pc) => chordSet.has(pc)).length;
      const containsAll = selected.length > 0 && matched === selected.length;
      const exact = containsAll && chord.pcs.length === selected.length
        && chord.pcs.every((pc) => selectedSet.has(pc));
      const score = (containsAll ? 500 : 0)
        + matched * 20
        + (selectedSet.has(chord.rootPc) ? 10 : 0)
        + (chord.group === "diatonic" ? 5 : 0);
      return {
        ...chord,
        matched,
        containsAll,
        exact,
        score,
        catalogIndex: chord.catalogIndex,
      };
    })
    .filter((chord) => !chord.exact)
    .sort((a, b) => b.score - a.score || a.catalogIndex - b.catalogIndex);
}

// A stable identity for a chord in the user's collected set: the same pitch
// classes are the same chord no matter which key/catalogue produced them.
export function chordSetKey(chord) {
  return uniquePcs(chord?.pcs ?? []).sort((a, b) => a - b).join("-");
}

// Reverse lookup for a collected chord set: every scale whose notes contain
// ALL of the collected chords (i.e. the union of their pitch classes). Only
// full fits are useful here — a "nearly fits" scale is not a scale where every
// selected chord works. Results keep the tighter-scales-first order from
// findScaleMatches.
export function scalesForChords(chords) {
  const union = uniquePcs((chords ?? []).flatMap((chord) => chord?.pcs ?? []));
  if (union.length < 2) return [];
  return findScaleMatches(union).filter((match) => match.containsAll);
}

export function spellSelectedNotes(pcs, rootPc, scaleId) {
  const parent = parentScaleId(scaleId);
  const selectedScale = scaleNotes(rootPc, scaleId);
  const parentScale = scaleNotes(rootPc, parent);
  const flats = prefersFlats(rootPc, parent);
  return uniquePcs(pcs)
    .sort((a, b) => a - b)
    .map((pc) => selectedScale.find((note) => note.pc === pc)?.name
      ?? parentScale.find((note) => note.pc === pc)?.name
      ?? spellPitchClass(pc, flats));
}

export function identifyChord(pcs, { rootPc = 0, scaleId = "major" } = {}) {
  const selected = uniquePcs(pcs);
  if (selected.length < 3 || selected.length > 4) return null;
  const selectedSet = new Set(selected);
  const scale = scaleNotes(rootPc, parentScaleId(scaleId));
  const scalePcs = new Set(scale.map((note) => note.pc));
  const candidates = [];

  for (let root = 0; root < 12; root += 1) {
    const templateMatches = IDENTIFY_TEMPLATES
      .map((template, templateRank) => ({ ...template, templateRank }))
      .filter((template) => template.intervals.length === selected.length)
      .filter((template) => {
        const candidate = new Set(template.intervals.map((interval) => mod12(root + interval)));
        return candidate.size === selectedSet.size && [...selectedSet].every((pc) => candidate.has(pc));
      });

    for (const template of templateMatches) {
      const info = QUALITY_INFO[template.quality];
      const rootNote = chordRootName(root, rootPc, scaleId);
      const notes = spellChordTones(rootNote, root, template.intervals, template.letters);
      candidates.push({
        symbol: `${rootNote}${info.suffix}`,
        name: `${rootNote} ${info.label}`,
        quality: template.quality,
        rootPc: root,
        pcs: template.intervals.map((interval) => mod12(root + interval)),
        notes,
        rootIsKey: mod12(root) === mod12(rootPc),
        rootInScale: scalePcs.has(mod12(root)),
        accidentalScore: notes.reduce((sum, note) => sum + accidentalWeight(note), 0),
        templateRank: template.templateRank,
      });
    }
  }

  if (candidates.length === 0) return null;

  candidates.sort((a, b) => (
    Number(b.rootIsKey) - Number(a.rootIsKey)
    || Number(b.rootInScale) - Number(a.rootInScale)
    || a.accidentalScore - b.accidentalScore
    || a.templateRank - b.templateRank
    || a.rootPc - b.rootPc
  ));

  const best = candidates[0];
  return {
    symbol: best.symbol,
    name: best.name,
    quality: best.quality,
    rootPc: best.rootPc,
    pcs: best.pcs,
    notes: best.notes,
  };
}
