#!/usr/bin/env node
// Regression check for the pure song-builder model (ticket 0065).
//
// The helpers live in WebUI/src/songModel.js (plain ESM, no DOM), so this
// script imports them directly and asserts templates, bar/duration math,
// editing helpers, chord suggestions, the chart text and normaliseSong.
// Run directly (`node Tests/check-song-model.mjs`) or via CTest
// (`SongModel`).

import {
  SECTION_BARS,
  SECTION_PRESETS,
  SONG_TEMPLATES,
  addSection,
  barSamples,
  chartText,
  chordTemplates,
  createSong,
  formatSongDuration,
  moveSection,
  newSongId,
  normaliseChords,
  normaliseSong,
  removeSection,
  sectionChordLine,
  sectionStartBar,
  sectionTone,
  setChordAtBar,
  setSectionBars,
  snippetBarsAtSongTempo,
  snippetIdsInOrder,
  songDurationSeconds,
  suggestChords,
  totalBars,
  updateSection,
} from "../WebUI/src/songModel.js";

let failures = 0;

function check(cond, message) {
  if (!cond) {
    failures += 1;
    console.error(`FAIL ${message}`);
  }
}

function eq(actual, expected, message) {
  const ok = JSON.stringify(actual) === JSON.stringify(expected);
  check(ok, `${message} (got ${JSON.stringify(actual)}, expected ${JSON.stringify(expected)})`);
}

const symbolsOf = (section) => section.chords.map((chord) => `${chord.bar}:${chord.symbol}`).join(" ");

// --- Constants ------------------------------------------------------------

eq(SECTION_BARS, [1, 2, 4, 8, 12, 16], "section bar values");
check(SECTION_PRESETS.length >= 4, "section presets exist");
eq(SECTION_PRESETS.map((preset) => `${preset.name}:${preset.bars}`),
  ["Intro:4", "Verse:8", "Pre-Chorus:4", "Chorus:8", "Bridge:8", "Solo:8", "Outro:4"],
  "section preset list");
eq(SONG_TEMPLATES.map((template) => template.id), ["pop", "blues", "blank"], "template ids");

// --- Templates ------------------------------------------------------------

const pop = createSong({ name: "Pop", keyRoot: 0, keyScale: "major", bpm: 120, template: "pop" });
eq(pop.sections.map((section) => `${section.name}:${section.bars}`),
  ["Intro:4", "Verse:8", "Chorus:8", "Verse:8", "Chorus:8", "Outro:4"], "pop sections");
eq(totalBars(pop), 40, "pop total bars");
eq(pop.sections[1].chords.map((chord) => chord.symbol), ["C", "G", "Am", "F"], "pop verse chords");
eq(pop.sections[1].chords.map((chord) => chord.bar), [0, 2, 4, 6], "pop verse chord bars");
check(pop.sections.every((section) => section.chords.length > 0), "pop every section has chords");
check(pop.sections.every((section) => section.id.startsWith("s")), "pop section ids");

const popMinor = createSong({ name: "Pop minor", keyRoot: 9, keyScale: "minor", bpm: 100, template: "pop" });
eq(popMinor.sections[1].chords.map((chord) => chord.symbol), ["Am", "F", "C", "G"], "pop minor verse chords");

const blues = createSong({ name: "Blues", keyRoot: 0, keyScale: "major", bpm: 90, template: "blues" });
eq(blues.sections.map((section) => `${section.name}:${section.bars}`),
  ["Intro:4", "12-bar 1:12", "12-bar 2:12", "Outro:4"], "blues sections");
eq(totalBars(blues), 32, "blues total bars");
eq(blues.sections[1].chords.length, 12, "blues 12 chord events");
eq(blues.sections[1].chords.map((chord) => chord.symbol),
  ["C", "C", "C", "C", "F", "F", "C", "C", "G", "F", "C", "G"], "blues 12-bar pattern");
eq(blues.sections[1].chords.map((chord) => chord.bar), [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11],
  "blues fixed bar positions");

const blank = createSong({ name: "Blank", template: "blank" });
eq(blank.sections.length, 1, "blank one section");
eq(blank.sections[0].bars, 8, "blank section bars");
eq(blank.sections[0].chords.length, 0, "blank no chords");

// --- Bar and duration math ------------------------------------------------

check(Math.abs(barSamples(120, { numerator: 4, denominator: 4 }, 48000) - 96000) < 1e-6,
  "4/4 bar samples at 120 BPM");
check(Math.abs(barSamples(120, { numerator: 6, denominator: 8 }, 48000) - 72000) < 1e-6,
  "6/8 bar samples at 120 BPM");
check(Math.abs(barSamples(60, { numerator: 4, denominator: 4 }, 44100) - 176400) < 1e-6,
  "4/4 bar samples at 60 BPM");
check(Math.abs(songDurationSeconds(pop) - 80) < 1e-6, "pop duration at 120 BPM");
eq(formatSongDuration(80), "1:20", "duration formatting");
eq(formatSongDuration(0), "0:00", "zero duration formatting");
eq(sectionStartBar(pop, 0), 0, "first section starts at 0");
eq(sectionStartBar(pop, 2), 12, "chorus starts after intro + verse");

eq(snippetBarsAtSongTempo({ durationSeconds: 8 }, pop), 4, "snippet bars at song tempo");
eq(snippetBarsAtSongTempo({ durationSeconds: 0 }, pop), 0, "unknown snippet length");
eq(snippetBarsAtSongTempo({ durationSeconds: 999 }, pop), 16, "snippet bars clamp");

// --- Editing helpers ------------------------------------------------------

let edited = addSection(pop);
eq(edited.sections.length, 7, "add section");
eq(edited.sections[6].name, "Section 7", "added section name");
check(!pop.sections.some((section) => section.name === "Section 7"), "addSection is immutable");

edited = addSection(pop, { name: "Bridge", bars: 4 });
eq(edited.sections[6].bars, 4, "added section bars");
eq(edited.sections[6].id.length > 0, true, "added section id");

eq(removeSection(pop, "s2").sections.map((section) => section.name),
  ["Intro", "Chorus", "Verse", "Chorus", "Outro"], "remove section");
eq(removeSection(pop, "missing"), pop, "remove unknown section is a no-op");

eq(moveSection(pop, "s2", -1).sections.map((section) => section.name),
  ["Verse", "Intro", "Chorus", "Verse", "Chorus", "Outro"], "move section up");
eq(moveSection(pop, "s2", 1).sections.map((section) => section.name),
  ["Intro", "Chorus", "Verse", "Verse", "Chorus", "Outro"], "move section down");
eq(moveSection(pop, "s1", -1), pop, "move out of range is a no-op");

const updated = updateSection(pop, "s1", { idea: "quiet", snippetId: 12 });
eq(updated.sections[0].idea, "quiet", "update section idea");
eq(updated.sections[0].snippetId, 12, "update section link");

// setSectionBars drops chord events past the new end.
const shortened = setSectionBars(pop, "s2", 2);
eq(shortened.sections[1].bars, 2, "section bars shortened");
eq(shortened.sections[1].chords.map((chord) => chord.bar), [0], "chords past the new end dropped");

// --- Chord normalisation --------------------------------------------------

eq(normaliseChords([
  { bar: 3, symbol: "G", name: "G major" },
  { bar: 0, symbol: "C", name: "C major" },
  { bar: 0, symbol: "D", name: "duplicate bar" },
  { bar: 9, symbol: "X", name: "out of range" },
  { bar: 1, symbol: "   ", name: "blank" },
], 4).map((chord) => `${chord.bar}:${chord.symbol}`), ["0:C", "3:G"], "normalise chords");

const chorded = setChordAtBar(blank, "s1", 2, { symbol: "Am", name: "A minor" });
eq(chorded.sections[0].chords[0].bar, 2, "set chord bar");
eq(chorded.sections[0].chords[0].symbol, "Am", "set chord symbol");
eq(setChordAtBar(chorded, "s1", 2, { symbol: "C" }).sections[0].chords.map((chord) => chord.symbol),
  ["C"], "replace chord at a bar");
eq(setChordAtBar(chorded, "s1", 99, { symbol: "C" }), chorded, "out-of-range chord is a no-op");
eq(setChordAtBar(chorded, "s1", 2, { symbol: "" }).sections[0].chords.length, 0,
  "blank chord clears the bar");
eq(sectionChordLine({ bars: 4, chords: [{ bar: 1, symbol: "Am" }] }), "- | Am | Am | Am",
  "held chord line");

// --- Suggestions ----------------------------------------------------------

const suggestSong = createSong({ name: "Suggest", keyRoot: 0, keyScale: "major", template: "blank" });
const majorSuggested = suggestChords(suggestSong, "s1", "I-V-vi-IV");
eq(majorSuggested.sections[0].chords.map((chord) => chord.symbol), ["C", "G", "Am", "F"],
  "major I-V-vi-IV suggestions");
eq(majorSuggested.sections[0].chords.map((chord) => chord.bar), [0, 2, 4, 6],
  "major suggestions placed evenly");

const minorSuggestSong = createSong({ name: "Minor", keyRoot: 9, keyScale: "minor", template: "blank" });
eq(suggestChords(minorSuggestSong, "s1", "i-VI-III-VII").sections[0].chords.map((chord) => chord.symbol),
  ["Am", "F", "C", "G"], "minor i-VI-III-VII suggestions");

// The minor 12-bar uses the borrowed harmonic-minor V, so a 12-bar section
// covers the whole turnaround (the 8-bar blank clips it before the V).
const minorTwelveSong = setSectionBars(minorSuggestSong, "s1", 12);
const minorTwelve = suggestChords(minorTwelveSong, "s1", "12-bar");
eq(minorTwelve.sections[0].chords.map((chord) => chord.symbol),
  ["Am", "Am", "Am", "Am", "Dm", "Dm", "Am", "Am", "E", "Dm", "Am", "E"],
  "minor 12-bar uses the borrowed harmonic-minor V");
eq(minorTwelve.sections[0].chords.map((chord) => chord.bar),
  [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11], "minor 12-bar fixed positions");

const twelve = suggestChords(suggestSong, "s1", "12-bar");
eq(twelve.sections[0].chords.length, 8, "12-bar clipped to an 8-bar section");
eq(twelve.sections[0].chords.map((chord) => chord.symbol),
  ["C", "C", "C", "C", "F", "F", "C", "C"], "12-bar first eight bars");

const wideTwelve = suggestChords(createSong({ name: "12", template: "blank", keyRoot: 0 }), "s1", "12-bar");
eq(wideTwelve.sections[0].chords.length, 8, "blank section is 8 bars");

eq(chordTemplates("major").map((template) => template.id),
  ["I-V-vi-IV", "I-IV-V", "vi-IV-I-V", "12-bar"], "major suggestion templates");
eq(chordTemplates("minor").map((template) => template.id),
  ["i-VI-III-VII", "i-iv-v", "12-bar"], "minor suggestion templates");
eq(suggestChords(suggestSong, "s1", "nope"), suggestSong, "unknown suggestion template");

// --- Chart text -----------------------------------------------------------

const chartSong = updateSection(
  updateSection(chorded, "s1", { name: "Verse", idea: "soft pick", snippetId: 4, recordedBars: 4 }),
  "s1",
  {},
);
const chart = chartText(chartSong, { 4: { id: 4, name: "Verse take" } });
check(chart.startsWith("# Blank"), "chart title");
check(chart.includes("C major"), "chart key");
check(chart.includes("8 bars"), "chart bar count");
check(chart.includes("## Verse (8 bars)"), "chart section heading");
check(chart.includes("- | - | Am | Am | Am | Am | Am | Am"), "chart chord line");
check(chart.includes("Idea: soft pick"), "chart idea");
check(chart.includes("Recording: Verse take"), "chart recording");
check(chartText(chartSong, {}).includes("Recording: (missing)"), "chart missing recording");
eq(chartText(null, {}), "", "chart of no song");

// --- Snippet order and tones ---------------------------------------------

const orderSong = updateSection(
  updateSection(updateSection(blank, "s1", { snippetId: 5 }), "s1", {}),
  "s1",
  {},
);
eq(snippetIdsInOrder(orderSong), [5], "snippet order single");
const multi = normaliseSong({
  name: "Order",
  sections: [
    { id: "a", name: "A", bars: 1, snippetId: 5 },
    { id: "b", name: "B", bars: 1, snippetId: 0 },
    { id: "c", name: "C", bars: 1, snippetId: 7 },
    { id: "d", name: "D", bars: 1, snippetId: 5 },
  ],
});
eq(snippetIdsInOrder(multi), [5, 7], "snippet order de-duplicated");

eq(sectionTone("Intro"), "intro", "tone intro");
eq(sectionTone("Chorus 2"), "chorus", "tone chorus");
eq(sectionTone("Pre-Chorus"), "other", "tone unknown");
eq(sectionTone(""), "other", "tone empty");

// --- normaliseSong --------------------------------------------------------

eq(normaliseSong(null), null, "normaliseSong rejects null");
eq(normaliseSong("nope"), null, "normaliseSong rejects a string");
eq(normaliseSong([]), null, "normaliseSong rejects an array");

const dirty = normaliseSong({
  name: "   ",
  keyRoot: 99,
  keyScale: "dorian",
  bpm: 900,
  numerator: 0,
  denominator: 3,
  sections: [
    { id: "bad id!", name: "", bars: 3, snippetId: -4, recordedBpm: 5, recordedBars: 99,
      chords: [{ bar: 0, symbol: "C" }, { bar: 99, symbol: "G" }] },
    { id: "dup", name: "Dup", bars: 1 },
    { id: "dup", name: "Dup 2", bars: 1 },
  ],
});
eq(dirty.name, "Untitled song", "normalise name fallback");
eq(dirty.keyRoot, 11, "normalise key root");
eq(dirty.keyScale, "major", "normalise scale");
eq(dirty.bpm, 300, "normalise bpm");
eq(dirty.numerator, 1, "normalise numerator");
eq(dirty.denominator, 4, "normalise denominator");
eq(dirty.sections[0].bars, 2, "normalise section bars");
eq(dirty.sections[0].name, "Section 1", "normalise section name");
eq(dirty.sections[0].snippetId, 0, "normalise negative snippet id");
eq(dirty.sections[0].recordedBpm, 20, "normalise recorded bpm");
eq(dirty.sections[0].recordedBars, 16, "normalise recorded bars");
eq(dirty.sections[0].chords.length, 1, "normalise drops out-of-range chords");
check(dirty.sections[1].id !== dirty.sections[2].id, "normalise duplicate section ids");

const capped = normaliseSong({
  name: "Capped",
  sections: Array.from({ length: 64 }, (_, index) => ({
    id: `s${index}`,
    name: `S${index}`,
    bars: 16,
  })),
});
let cappedBars = 0;
for (const section of capped.sections) cappedBars += section.bars;
check(cappedBars <= 512, "normalise total bar cap");
check(capped.sections.length <= 64, "normalise section cap");

// --- Ids ------------------------------------------------------------------

const id = newSongId();
check(id.length > 0 && id.length <= 32, "new song id length");
check(/^[A-Za-z0-9_-]+$/.test(id), "new song id charset");
check(newSongId() !== newSongId(), "new song ids differ");

if (failures === 0) {
  console.log("ok   SongModel: all song-builder checks passed");
  process.exit(0);
}

console.error(`FAIL SongModel: ${failures} check(s) failed`);
process.exit(1);
