#!/usr/bin/env node
// Regression check for the pure songwriting chord theory (ticket 0058).
//
// The helpers live in WebUI/src/chords.js (plain ESM, no DOM), so this script
// imports them directly and asserts the diatonic/borrowed catalogues, ranking,
// chord recognition and key-aware spelling. Run directly
// (`node Tests/check-chord-theory.mjs`) or via CTest (`ChordTheory`).

import {
  buildChordCatalog,
  chordSetKey,
  findScaleMatches,
  identifyChord,
  parentKeyLabel,
  parentScaleId,
  rankChords,
  scalesForChords,
  spellSelectedNotes,
} from "../WebUI/src/chords.js";

let failures = 0;

function check(cond, message) {
  if (!cond) {
    failures += 1;
    console.error(`FAIL ${message}`);
  }
}

function sameSet(actual, expected) {
  const a = [...new Set(actual)].sort((x, y) => x - y);
  const e = [...new Set(expected)].sort((x, y) => x - y);
  return a.length === e.length && a.every((value, index) => value === e[index]);
}

function bySymbol(catalog, symbol) {
  return catalog.find((chord) => chord.symbol === symbol);
}

function symbols(catalog) {
  return catalog.map((chord) => chord.symbol).join(" ");
}

function romans(catalog) {
  return catalog.map((chord) => chord.roman).join(" ");
}

// --- Parent scale mapping -------------------------------------------------

check(parentScaleId("major") === "major", "major parent");
check(parentScaleId("majorPentatonic") === "major", "major pentatonic parent");
check(parentScaleId("minor") === "minor", "minor parent");
check(parentScaleId("minorPentatonic") === "minor", "minor pentatonic parent");
check(parentScaleId("blues") === "minor", "blues parent");
check(parentKeyLabel(0, "majorPentatonic") === "C major", "C major pentatonic label");
check(parentKeyLabel(9, "blues") === "A minor", "A blues label");
check(parentKeyLabel(6, "minorPentatonic") === "F# minor", "F# minor pentatonic label");

const pentatonicCatalog = buildChordCatalog(0, "majorPentatonic")
  .filter((chord) => chord.group === "diatonic");
check(symbols(pentatonicCatalog) === "C Dm Em F G Am Bdim", "pentatonic uses the parent major catalogue");
const bluesCatalog = buildChordCatalog(9, "blues")
  .filter((chord) => chord.group === "diatonic");
check(symbols(bluesCatalog) === "Am Bdim C Dm Em F G", "blues uses the parent minor catalogue");

// --- Diatonic catalogues --------------------------------------------------

const cMajor = buildChordCatalog(0, "major");
const cMajorDiatonic = cMajor.filter((chord) => chord.group === "diatonic");
check(cMajorDiatonic.length === 7, "C major: seven diatonic chords");
check(symbols(cMajorDiatonic) === "C Dm Em F G Am Bdim", "C major triads");
check(romans(cMajorDiatonic) === "I ii iii IV V vi vii°", "C major roman numerals");
check(cMajorDiatonic[0].notes.join(" ") === "C E G", "C major spelling");
check(cMajorDiatonic[1].notes.join(" ") === "D F A", "D minor spelling");
check(cMajorDiatonic[6].notes.join(" ") === "B D F", "B diminished spelling");
check(cMajorDiatonic.every((chord) => chord.group === "diatonic"), "C major group");

[
  ["major", [0, 4, 7]],
  ["minor", [2, 5, 9]],
  ["minor", [4, 7, 11]],
  ["major", [5, 9, 0]],
  ["major", [7, 11, 2]],
  ["minor", [9, 0, 4]],
  ["diminished", [11, 2, 5]],
].forEach(([quality, pcs], index) => {
  check(cMajorDiatonic[index].quality === quality, `C major degree ${index + 1} quality`);
  check(sameSet(cMajorDiatonic[index].pcs, pcs), `C major degree ${index + 1} pitch classes`);
});

const cMajor7 = buildChordCatalog(0, "major", { sevenths: true });
const cMajor7Diatonic = cMajor7.filter((chord) => chord.group === "diatonic");
check(cMajor7Diatonic.length === 7, "C major sevenths: seven chords");
check(symbols(cMajor7Diatonic) === "Cmaj7 Dm7 Em7 Fmaj7 G7 Am7 Bm7b5", "C major seventh symbols");
check(romans(cMajor7Diatonic) === "Imaj7 ii7 iii7 IVmaj7 V7 vi7 viiø7", "C major seventh romans");
check(cMajor7Diatonic[0].notes.join(" ") === "C E G B", "Cmaj7 spelling");
check(cMajor7Diatonic[6].notes.join(" ") === "B D F A", "Bm7b5 spelling");

[
  ["major7", [0, 4, 7, 11]],
  ["minor7", [2, 5, 9, 0]],
  ["minor7", [4, 7, 11, 2]],
  ["major7", [5, 9, 0, 4]],
  ["dominant7", [7, 11, 2, 5]],
  ["minor7", [9, 0, 4, 7]],
  ["halfDiminished7", [11, 2, 5, 9]],
].forEach(([quality, pcs], index) => {
  check(cMajor7Diatonic[index].quality === quality, `C major seventh degree ${index + 1} quality`);
  check(sameSet(cMajor7Diatonic[index].pcs, pcs), `C major seventh degree ${index + 1} pitch classes`);
});

const aMinor = buildChordCatalog(9, "minor").filter((chord) => chord.group === "diatonic");
check(symbols(aMinor) === "Am Bdim C Dm Em F G", "A minor triads");
check(romans(aMinor) === "i ii° III iv v VI VII", "A minor roman numerals");
check(aMinor[0].notes.join(" ") === "A C E", "A minor spelling");
check(aMinor[1].notes.join(" ") === "B D F", "B diminished spelling");

[
  ["minor", [9, 0, 4]],
  ["diminished", [11, 2, 5]],
  ["major", [0, 4, 7]],
  ["minor", [2, 5, 9]],
  ["minor", [4, 7, 11]],
  ["major", [5, 9, 0]],
  ["major", [7, 11, 2]],
].forEach(([quality, pcs], index) => {
  check(aMinor[index].quality === quality, `A minor degree ${index + 1} quality`);
  check(sameSet(aMinor[index].pcs, pcs), `A minor degree ${index + 1} pitch classes`);
});

const aMinor7 = buildChordCatalog(9, "minor", { sevenths: true })
  .filter((chord) => chord.group === "diatonic");
check(symbols(aMinor7) === "Am7 Bm7b5 Cmaj7 Dm7 Em7 Fmaj7 G7", "A minor seventh symbols");
check(romans(aMinor7) === "i7 iiø7 IIImaj7 iv7 v7 VImaj7 VII7", "A minor seventh romans");

[
  ["minor7", [9, 0, 4, 7]],
  ["halfDiminished7", [11, 2, 5, 9]],
  ["major7", [0, 4, 7, 11]],
  ["minor7", [2, 5, 9, 0]],
  ["minor7", [4, 7, 11, 2]],
  ["major7", [5, 9, 0, 4]],
  ["dominant7", [7, 11, 2, 5]],
].forEach(([quality, pcs], index) => {
  check(aMinor7[index].quality === quality, `A minor seventh degree ${index + 1} quality`);
  check(sameSet(aMinor7[index].pcs, pcs), `A minor seventh degree ${index + 1} pitch classes`);
});

// --- Borrowed catalogue ---------------------------------------------------

const cBorrowed = buildChordCatalog(0, "major").filter((chord) => chord.group === "borrowed");
check(cBorrowed.length === 4, "C major: four borrowed chords");
check(symbols(cBorrowed) === "Fm Ab Bb D", "C major borrowed symbols");
check(romans(cBorrowed) === "iv bVI bVII II", "C major borrowed romans");
check(bySymbol(cBorrowed, "Fm").notes.join(" ") === "F Ab C", "borrowed iv spelling");
check(bySymbol(cBorrowed, "Ab").notes.join(" ") === "Ab C Eb", "borrowed bVI spelling");
check(bySymbol(cBorrowed, "Bb").notes.join(" ") === "Bb D F", "borrowed bVII spelling");
check(bySymbol(cBorrowed, "D").notes.join(" ") === "D F# A", "borrowed V/V spelling");

const cBorrowed7 = buildChordCatalog(0, "major", { sevenths: true })
  .filter((chord) => chord.group === "borrowed");
check(symbols(cBorrowed7) === "Fm7 Abmaj7 Bb7 D7", "C major borrowed sevenths");
check(bySymbol(cBorrowed7, "D7").notes.join(" ") === "D F# A C", "borrowed V/V seventh spelling");

const aBorrowed = buildChordCatalog(9, "minor").filter((chord) => chord.group === "borrowed");
check(aBorrowed.length === 4, "A minor: four borrowed chords");
check(symbols(aBorrowed) === "D E A Bb", "A minor borrowed symbols");
check(romans(aBorrowed) === "IV V I bII", "A minor borrowed romans");
check(bySymbol(aBorrowed, "E").notes.join(" ") === "E G# B", "harmonic-minor V spelling");
check(bySymbol(aBorrowed, "A").notes.join(" ") === "A C# E", "Picardy I spelling");
check(bySymbol(aBorrowed, "Bb").notes.join(" ") === "Bb D F", "Neapolitan spelling");

// --- All roots ------------------------------------------------------------

for (const scaleId of ["major", "minor"]) {
  for (let root = 0; root < 12; root += 1) {
    for (const sevenths of [false, true]) {
      const catalog = buildChordCatalog(root, scaleId, { sevenths });
      const diatonic = catalog.filter((chord) => chord.group === "diatonic");
      const borrowed = catalog.filter((chord) => chord.group === "borrowed");
      check(diatonic.length === 7, `${scaleId} ${root} ${sevenths ? "7" : "triad"}: seven diatonic`);
      check(borrowed.length === 4, `${scaleId} ${root} ${sevenths ? "7" : "triad"}: four borrowed`);
      check(catalog.every((chord) => chord.pcs.length === (sevenths ? 4 : 3)),
        `${scaleId} ${root} ${sevenths ? "7" : "triad"}: chord tone count`);
      check(catalog.every((chord) => new Set(chord.pcs).size === chord.pcs.length),
        `${scaleId} ${root} ${sevenths ? "7" : "triad"}: unique pcs`);
      check(catalog.every((chord) => chord.pcs[0] === chord.rootPc),
        `${scaleId} ${root} ${sevenths ? "7" : "triad"}: starts on root`);
      check(catalog.every((chord) => /^[A-G](b|#|##|bb)?$/.test(chord.symbol.replace(/(maj7|m7b5|dim7|mMaj7|dim|aug|m7|m|7)$/, ""))),
        `${scaleId} ${root} ${sevenths ? "7" : "triad"}: root spelling`);
    }
  }
}

// Sharp and flat keys keep their conventional spellings.
check(bySymbol(buildChordCatalog(2, "major"), "D").notes.join(" ") === "D F# A", "D major tonic");
check(bySymbol(buildChordCatalog(5, "major"), "F").notes.join(" ") === "F A C", "F major tonic");
check(buildChordCatalog(5, "major")[1].notes.join(" ") === "G Bb D", "F major ii spelling");
check(bySymbol(buildChordCatalog(1, "major"), "Ebm").notes.join(" ") === "Eb Gb Bb", "Db major ii spelling");

// Selected-note labels follow the visible scale first (Eb minor's black keys
// are not respelled as sharps), then the parent scale, then key preference.
check(spellSelectedNotes([1, 3, 6, 8, 10], 3, "minor").join(" ") === "Db Eb Gb Ab Bb",
  "Eb minor selected-note spelling");
check(spellSelectedNotes([3], 9, "blues").join(" ") === "Eb", "A blues flat-five spelling");
check(spellSelectedNotes([11], 9, "blues").join(" ") === "B", "blues falls back to parent spelling");

// --- Reverse scale lookup -------------------------------------------------

check(findScaleMatches([]).length === 0, "scale lookup: no notes");
check(findScaleMatches([0]).length === 0, "scale lookup: one note is too broad");

const cegScales = findScaleMatches([0, 4, 7]);
check(cegScales.length > 0, "scale lookup: C E G has matches");
check(cegScales.some((match) => match.name === "C Major" && match.containsAll), "scale lookup: C Major");
check(cegScales.some((match) => match.name === "A Natural minor" && match.containsAll), "scale lookup: A Natural minor");
check(cegScales[0].containsAll, "scale lookup: full fits first");
check(cegScales.every((match, index) => index === 0 || cegScales[index - 1].matched >= match.matched),
  "scale lookup: ranked by matched tones");
const fullCeg = cegScales.filter((match) => match.containsAll);
check(fullCeg.every((match, index) => index === 0 || fullCeg[index - 1].size <= match.size),
  "scale lookup: tighter full fits first");
check(cegScales.every((match) => match.selectedCount === 3), "scale lookup: selected count");

const chromaticScales = findScaleMatches([0, 1, 2, 3]);
check(chromaticScales.length > 0, "scale lookup: chromatic partial fits");
check(chromaticScales.every((match) => !match.containsAll), "scale lookup: no full fit for C C# D D#");
check(chromaticScales.every((match, index) => index === 0 || chromaticScales[index - 1].matched >= match.matched),
  "scale lookup: partial fits ranked");

// --- Chord-set scale lookup ----------------------------------------------

check(scalesForChords([]).length === 0, "chord set: no chords is empty");
check(scalesForChords([bySymbol(cMajor, "C")]).length > 0, "chord set: a single chord still finds scales");

const cAndGChords = [bySymbol(cMajor, "C"), bySymbol(cMajor, "G")];
const cAndGScales = scalesForChords(cAndGChords);
check(cAndGScales.length > 0, "chord set: C + G has scales");
check(cAndGScales.every((match) => match.containsAll), "chord set: only full fits");
check(cAndGScales.every((match) => [0, 4, 7, 11, 2].every((pc) => match.pcs.includes(pc))),
  "chord set: contains every tone of every chord");
check(cAndGScales.some((match) => match.name === "C Major"), "chord set: C Major fits C + G");
check(cAndGScales.every((match, index) => index === 0 || cAndGScales[index - 1].size <= match.size),
  "chord set: tighter scales first");

const cAndFsharp = scalesForChords([
  bySymbol(cMajor, "C"),
  bySymbol(buildChordCatalog(6, "major"), "F#"),
]);
check(cAndFsharp.every((match) => match.containsAll),
  "chord set: chromatic pairs only return full fits");

check(chordSetKey({ pcs: [0, 4, 7] }) === chordSetKey({ pcs: [7, 0, 4] }),
  "chord set key: order-independent");
check(chordSetKey({ pcs: [0, 4, 7] }) !== chordSetKey({ pcs: [0, 3, 7] }),
  "chord set key: distinguishes qualities");

// --- Ranking --------------------------------------------------------------

const emptyRanked = rankChords(cMajor, []);
check(emptyRanked.length === cMajor.length, "empty selection keeps every suggestion");
check(emptyRanked.map((chord) => chord.symbol).join(" ") === symbols(cMajor), "empty selection keeps order");

const exactRanked = rankChords(cMajor, [0, 4, 7]);
check(!exactRanked.some((chord) => chord.symbol === "C"), "exact current chord is omitted");
check(exactRanked.every((chord) => chord.score >= 0), "ranked suggestions carry scores");
check(exactRanked[0].matched >= exactRanked[1].matched, "ranking orders by matched tones");

const partialRanked = rankChords(cMajor, [0, 4]);
check(partialRanked[0].symbol === "C", "partial match: full-chord candidate first");
check(partialRanked[0].containsAll === true, "partial match: contains-all flag");
check(partialRanked[0].matched === 2, "partial match: two selected tones");

// Ties are stable: the catalogue order wins when the score is identical.
const tieCatalog = [
  { ...cMajor[2], symbol: "TieA", pcs: [0, 4, 7], rootPc: 0, group: "diatonic", catalogIndex: 0 },
  { ...cMajor[3], symbol: "TieB", pcs: [0, 4, 7], rootPc: 0, group: "diatonic", catalogIndex: 1 },
];
const tieRanked = rankChords(tieCatalog, [0, 4]);
check(tieRanked.length === 2 && tieRanked[0].symbol === "TieA" && tieRanked[1].symbol === "TieB",
  "ties keep catalogue order");

// --- Chord identification -------------------------------------------------

const cMajorId = identifyChord([0, 4, 7], { rootPc: 0, scaleId: "major" });
check(cMajorId && cMajorId.symbol === "C" && cMajorId.quality === "major", "identify C major");
check(cMajorId.notes.join(" ") === "C E G", "identify C major spelling");

check(identifyChord([0, 3, 7]).symbol === "Cm", "identify C minor");
check(identifyChord([0, 4, 8]).symbol === "Caug", "identify C augmented (key root wins)");
check(identifyChord([0, 2, 7]).symbol === "Csus2", "identify C sus2");
check(identifyChord([0, 5, 7]).symbol === "Csus4", "identify C sus4");
check(identifyChord([0, 4, 7, 11]).symbol === "Cmaj7", "identify Cmaj7");
check(identifyChord([0, 4, 7, 10]).symbol === "C7", "identify C7");
check(identifyChord([9, 0, 4, 7]).symbol === "Am7", "identify Am7");
check(identifyChord([11, 2, 5]).symbol === "Bdim", "identify B diminished");
check(identifyChord([11, 2, 5, 9]).symbol === "Bm7b5", "identify B half-diminished");

const eMajor = identifyChord([4, 8, 11], { rootPc: 9, scaleId: "minor" });
check(eMajor && eMajor.symbol === "E" && eMajor.notes.join(" ") === "E G# B",
  "identify harmonic-minor V spelling");

const abMajor = identifyChord([8, 0, 3], { rootPc: 0, scaleId: "major" });
check(abMajor && abMajor.symbol === "Ab" && abMajor.notes.join(" ") === "Ab C Eb",
  "identify chromatic bVI spelling");

check(identifyChord([0, 1]) === null, "reject two-note selection");
check(identifyChord([0, 2, 4, 6, 8, 10]) === null, "reject unknown six-note cluster");

if (failures === 0) {
  console.log("ok   ChordTheory: all chord/ranking checks passed");
  process.exit(0);
}

console.error(`FAIL ChordTheory: ${failures} check(s) failed`);
process.exit(1);
