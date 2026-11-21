// Pure song-builder model (0065).
//
// A song is a planning document: name, key, tempo, meter and ordered
// sections. Each section carries a bar count, at most one chord event per
// bar, a melody/idea line and one linked library snippet. This module is
// plain ESM (no DOM, no React) so the backend contract, the Song tab and
// the SongModel CTest all share one implementation. The C++ SongStore
// enforces the same limits; normaliseSong is the defensive mirror.

import { buildChordCatalog } from "./chords.js";
import { rootName } from "./utils.js";

export const SECTION_BARS = [1, 2, 4, 8, 12, 16];

export const SECTION_PRESETS = [
  { id: "intro", name: "Intro", bars: 4 },
  { id: "verse", name: "Verse", bars: 8 },
  { id: "prechorus", name: "Pre-Chorus", bars: 4 },
  { id: "chorus", name: "Chorus", bars: 8 },
  { id: "bridge", name: "Bridge", bars: 8 },
  { id: "solo", name: "Solo", bars: 8 },
  { id: "outro", name: "Outro", bars: 4 },
];

export const SONG_TEMPLATES = [
  { id: "pop", name: "Pop", description: "Intro, two verses, two choruses, outro" },
  { id: "blues", name: "Blues", description: "Intro, two 12-bar rounds, outro" },
  { id: "blank", name: "Blank", description: "One 8-bar section" },
];

const MAX_SECTIONS = 64;
const MAX_TOTAL_BARS = 512;
const MAX_CHORDS = 32;
const MAX_NAME = 80;
const MAX_SECTION_NAME = 40;
const MAX_IDEA = 200;
const MAX_SYMBOL = 12;
const MAX_CHORD_NAME = 40;
const MAX_ID = 32;

const DENOMINATORS = [1, 2, 4, 8, 16];
const ID_CHARS = /[^A-Za-z0-9_-]/g;

const MAJOR_PROGRESSIONS = [
  { id: "I-V-vi-IV", name: "I-V-vi-IV", degrees: ["I", "V", "vi", "IV"] },
  { id: "I-IV-V", name: "I-IV-V", degrees: ["I", "IV", "V"] },
  { id: "vi-IV-I-V", name: "vi-IV-I-V", degrees: ["vi", "IV", "I", "V"] },
];

const MINOR_PROGRESSIONS = [
  { id: "i-VI-III-VII", name: "i-VI-III-VII", degrees: ["i", "VI", "III", "VII"] },
  { id: "i-iv-v", name: "i-iv-v", degrees: ["i", "iv", "v"] },
];

// Fixed one-chord-per-bar positions. The minor version uses the borrowed
// harmonic-minor V (the catalogue's "V"), the conventional turnaround.
const TWELVE_BAR = {
  id: "12-bar",
  name: "12-bar",
  major: ["I", "I", "I", "I", "IV", "IV", "I", "I", "V", "IV", "I", "V"],
  minor: ["i", "i", "i", "i", "iv", "iv", "i", "i", "V", "iv", "i", "V"],
};

// The template sections. `degrees` is placed evenly across the section; a
// `twelveBar` section uses the fixed 12-bar positions instead.
const POP_SECTIONS = [
  { name: "Intro", bars: 4, major: ["I"], minor: ["i"] },
  { name: "Verse", bars: 8, major: ["I", "V", "vi", "IV"], minor: ["i", "VI", "III", "VII"] },
  { name: "Chorus", bars: 8, major: ["vi", "IV", "I", "V"], minor: ["i", "iv", "v"] },
  { name: "Verse", bars: 8, major: ["I", "V", "vi", "IV"], minor: ["i", "VI", "III", "VII"] },
  { name: "Chorus", bars: 8, major: ["vi", "IV", "I", "V"], minor: ["i", "iv", "v"] },
  { name: "Outro", bars: 4, major: ["I", "IV", "V", "I"], minor: ["i", "iv", "V", "i"] },
];

const BLUES_SECTIONS = [
  { name: "Intro", bars: 4, major: ["I"], minor: ["i"] },
  { name: "12-bar 1", bars: 12, twelveBar: true },
  { name: "12-bar 2", bars: 12, twelveBar: true },
  { name: "Outro", bars: 4, major: ["I"], minor: ["i"] },
];

function clampInt(value, lo, hi, fallback) {
  const number = Number(value);
  if (!Number.isFinite(number)) return fallback;
  return Math.min(hi, Math.max(lo, Math.trunc(number)));
}

function clampNumber(value, lo, hi, fallback) {
  const number = Number(value);
  if (!Number.isFinite(number)) return fallback;
  return Math.min(hi, Math.max(lo, number));
}

function trimmedTo(value, maxChars) {
  return String(value ?? "").trim().slice(0, maxChars);
}

export function sanitiseId(raw) {
  return String(raw ?? "").replace(ID_CHARS, "").slice(0, MAX_ID);
}

// Section bars are loopable counts: the nearest allowed value in 1..16.
export function normaliseBars(value) {
  const target = clampInt(value, 1, 16, 8);
  let best = SECTION_BARS[0];
  let bestDistance = Math.abs(best - target);
  for (const candidate of SECTION_BARS) {
    const distance = Math.abs(candidate - target);
    if (distance < bestDistance) {
      best = candidate;
      bestDistance = distance;
    }
  }
  return best;
}

export function newSongId() {
  // Timestamp + random base36: localStorage-safe and unique enough for a
  // single user's document list (the backend id charset is [A-Za-z0-9_-]).
  return `${Date.now().toString(36)}${Math.random().toString(36).slice(2, 6)}`.slice(0, MAX_ID);
}

export function normaliseChords(chords, bars) {
  const limit = Math.max(1, Number(bars) || 1);
  const out = [];
  const seen = new Set();
  for (const raw of Array.isArray(chords) ? chords : []) {
    if (out.length >= MAX_CHORDS) break;
    const bar = Math.trunc(Number(raw?.bar));
    if (!Number.isFinite(bar) || bar < 0 || bar >= limit || seen.has(bar)) continue;
    const symbol = trimmedTo(raw?.symbol, MAX_SYMBOL);
    if (!symbol) continue;
    seen.add(bar);
    out.push({ bar, symbol, name: trimmedTo(raw?.name, MAX_CHORD_NAME) });
  }
  return out.sort((a, b) => a.bar - b.bar);
}

function sectionIdFallback(sections, index) {
  const used = new Set(sections.map((section) => section.id));
  let n = index + 1;
  while (used.has(`s${n}`)) n += 1;
  return `s${n}`;
}

// Defensive parse of one song payload. Non-objects are rejected (null);
// every other field is coerced to the same limits SongStore enforces.
export function normaliseSong(raw) {
  if (!raw || typeof raw !== "object" || Array.isArray(raw)) return null;

  const song = {
    version: 1,
    id: sanitiseId(raw.id) || newSongId(),
    name: trimmedTo(raw.name, MAX_NAME) || "Untitled song",
    keyRoot: clampInt(raw.keyRoot, 0, 11, 0),
    keyScale: raw.keyScale === "minor" ? "minor" : "major",
    bpm: clampNumber(raw.bpm, 20, 300, 120),
    numerator: clampInt(raw.numerator, 1, 16, 4),
    denominator: DENOMINATORS.includes(Number(raw.denominator)) ? Number(raw.denominator) : 4,
    sections: [],
  };

  const rawSections = Array.isArray(raw.sections) ? raw.sections.slice(0, MAX_SECTIONS) : [];
  let totalBars = 0;

  for (const rawSection of rawSections) {
    if (!rawSection || typeof rawSection !== "object") continue;
    const bars = normaliseBars(rawSection.bars);
    if (totalBars + bars > MAX_TOTAL_BARS) break;
    totalBars += bars;

    let id = sanitiseId(rawSection.id);
    if (!id || song.sections.some((section) => section.id === id))
      id = sectionIdFallback(song.sections, song.sections.length);

    const recordedBpm = Number(rawSection.recordedBpm);
    song.sections.push({
      id,
      name: trimmedTo(rawSection.name, MAX_SECTION_NAME) || `Section ${song.sections.length + 1}`,
      bars,
      chords: normaliseChords(rawSection.chords, bars),
      idea: trimmedTo(rawSection.idea, MAX_IDEA),
      snippetId: clampInt(rawSection.snippetId, 0, Number.MAX_SAFE_INTEGER, 0),
      recordedBpm: Number.isFinite(recordedBpm) && recordedBpm > 0
        ? clampNumber(recordedBpm, 20, 300, 0)
        : 0,
      recordedBars: clampInt(rawSection.recordedBars, 0, 16, 0),
    });
  }

  return song;
}

// --- Derived values -------------------------------------------------------

export function totalBars(song) {
  return (song?.sections ?? []).reduce((sum, section) => sum + (Number(section.bars) || 0), 0);
}

// A bar is `numerator * (4 / denominator)` quarter notes.
export function barSamples(bpm, meter, sampleRate) {
  const tempo = clampNumber(bpm, 20, 300, 120);
  const numerator = clampInt(meter?.numerator, 1, 16, 4);
  const denominator = DENOMINATORS.includes(Number(meter?.denominator)) ? Number(meter.denominator) : 4;
  return (60 / tempo) * (Number(sampleRate) || 0) * numerator * (4 / denominator);
}

function barSeconds(bpm, meter) {
  return barSamples(bpm, meter, 1);
}

export function songDurationSeconds(song) {
  if (!song) return 0;
  return totalBars(song) * barSeconds(song.bpm, song);
}

export function sectionStartBar(song, index) {
  const sections = song?.sections ?? [];
  let start = 0;
  for (let i = 0; i < index && i < sections.length; i += 1)
    start += Number(sections[i].bars) || 0;
  return start;
}

// --- Rehearsal (0066) ------------------------------------------------------

// The chord held at `bar`: the last chord event at or before it. Chord events
// are sorted by normaliseChords, but the scan doesn't rely on it.
function heldChordAt(section, bar) {
  let held = null;
  for (const chord of section?.chords ?? []) {
    const at = Number(chord?.bar);
    if (!Number.isFinite(at) || at > bar) continue;
    if (!held || at >= Number(held.bar)) held = chord;
  }
  return held;
}

// The section that owns `bar` (bar < totalBars), plus its first bar.
function sectionAtBar(sections, bar) {
  let start = 0;
  let index = 0;
  for (let i = 0; i < sections.length; i += 1) {
    const count = Number(sections[i].bars) || 0;
    index = i;
    if (bar < start + count) return { index: i, start };
    start += count;
  }
  return { index, start };
}

// Where the click-only rehearsal is: the song bar under `positionSamples`
// (the metronome beat clock), the section that owns it and the chord held
// there. `startSample` is the absolute position the run began at (the
// section's first bar); the one-bar `leadInSamples` count-in plays there and
// the song starts on the next downbeat, so the run is mapped relative to
// `startSample`, never from bar 0. During the lead-in the phase is "countin"
// and the section is the one the run started in. A position past the last bar
// reports "done" — the caller stops the transport once. Invalid song/bar math
// reports "idle".
export function rehearsalAt(song, positionSamples, { startSample = 0, leadInSamples = 0, barSamples = 0 } = {}) {
  const sections = song?.sections ?? [];
  const bars = totalBars(song);
  const barLength = Number(barSamples);
  const position = Number(positionSamples);
  if (sections.length === 0 || bars <= 0
      || !Number.isFinite(barLength) || barLength <= 0 || !Number.isFinite(position))
    return { phase: "idle" };

  const leadIn = Math.max(0, Number(leadInSamples) || 0);
  const runStart = Number.isFinite(Number(startSample)) ? Math.max(0, Number(startSample)) : 0;
  const relative = Math.max(0, position - runStart);
  const startBar = Math.min(bars - 1, Math.floor(runStart / barLength));
  const startedIn = sectionAtBar(sections, startBar);

  if (relative < leadIn) {
    // One full bar of lead-in: count the beats of the song's meter so the
    // readout lines up with the click accents. The lead-in bar is the
    // section's first bar, so the highlight is already on the right section.
    const numerator = Math.max(1, Number(song?.numerator) || 4);
    const beatLength = barLength / numerator;
    const beat = Math.min(numerator, Math.floor(relative / beatLength) + 1);
    return {
      phase: "countin",
      countInBeat: Math.max(1, beat),
      sectionIndex: startedIn.index,
      barInSection: 0,
      barInSong: startBar,
      chord: null,
    };
  }

  const songBar = startBar + Math.floor((relative - leadIn) / barLength);
  const lastIndex = sections.length - 1;
  const lastSection = sections[lastIndex];
  const lastBarInSection = Math.max(0, (Number(lastSection.bars) || 1) - 1);

  if (songBar >= bars) {
    // Past the end: keep naming the last bar so the readout doesn't blank
    // while the stop round-trip lands.
    return {
      phase: "done",
      sectionIndex: lastIndex,
      barInSection: lastBarInSection,
      barInSong: bars - 1,
      chord: heldChordAt(lastSection, lastBarInSection),
    };
  }

  const found = sectionAtBar(sections, songBar);
  const barInSection = songBar - found.start;
  return {
    phase: "playing",
    sectionIndex: found.index,
    barInSection,
    barInSong: songBar,
    chord: heldChordAt(sections[found.index], barInSection),
  };
}

// The metronome-position sample a rehearsal starts at: the section's first
// bar. That bar is also the one-bar count-in lead-in, so the song itself
// starts one bar later at `rehearsalStartSample(...) + leadInSamples`.
// Section 0 is the top; never negative.
export function rehearsalStartSample(song, sectionIndex, barSamples) {
  const barLength = Number(barSamples);
  if (!song || !Number.isFinite(barLength) || barLength <= 0) return 0;
  const index = Math.max(0, Math.trunc(Number(sectionIndex) || 0));
  return Math.max(0, sectionStartBar(song, index) * barLength);
}

export function formatSongDuration(seconds) {
  const total = Math.max(0, Math.round(Number(seconds) || 0));
  const minutes = Math.floor(total / 60);
  const rest = total % 60;
  return `${minutes}:${String(rest).padStart(2, "0")}`;
}

// Bars a snippet would occupy at the song's tempo/meter (rounded, 1..16).
// Snippets carry no tempo of their own, so this is an estimate only.
export function snippetBarsAtSongTempo(snippet, song) {
  const duration = Number(snippet?.durationSeconds);
  if (!song || !Number.isFinite(duration) || duration <= 0) return 0;
  const secondsPerBar = barSeconds(song.bpm, song);
  if (!(secondsPerBar > 0)) return 0;
  return Math.min(16, Math.max(1, Math.round(duration / secondsPerBar)));
}

// --- Editing helpers (all immutable) -------------------------------------

function withSections(song, sections) {
  if (!song) return song;
  return normaliseSong({ ...song, sections }) ?? song;
}

function mapSection(song, sectionId, update) {
  if (!song) return song;
  let found = false;
  const sections = song.sections.map((section) => {
    if (section.id !== sectionId) return section;
    found = true;
    return update(section);
  });
  return found ? withSections(song, sections) : song;
}

export function addSection(song, section = {}) {
  if (!song) return song;
  const id = sanitiseId(section.id)
    || sectionIdFallback(song.sections, song.sections.length);
  const next = {
    id,
    name: trimmedTo(section.name, MAX_SECTION_NAME) || `Section ${song.sections.length + 1}`,
    bars: section.bars ?? 8,
    chords: section.chords ?? [],
    idea: section.idea ?? "",
    snippetId: section.snippetId ?? 0,
    recordedBpm: section.recordedBpm ?? 0,
    recordedBars: section.recordedBars ?? 0,
  };
  return withSections(song, [...song.sections, next]);
}

export function removeSection(song, sectionId) {
  if (!song) return song;
  const sections = song.sections.filter((section) => section.id !== sectionId);
  return sections.length === song.sections.length ? song : withSections(song, sections);
}

export function moveSection(song, sectionId, delta) {
  if (!song) return song;
  const index = song.sections.findIndex((section) => section.id === sectionId);
  const target = index + Math.trunc(Number(delta) || 0);
  if (index < 0 || target < 0 || target >= song.sections.length) return song;
  const sections = [...song.sections];
  const [moved] = sections.splice(index, 1);
  sections.splice(target, 0, moved);
  return withSections(song, sections);
}

export function updateSection(song, sectionId, patch = {}) {
  return mapSection(song, sectionId, (section) => ({
    ...section,
    ...patch,
    id: section.id,
  }));
}

export function setSectionBars(song, sectionId, bars) {
  const nextBars = normaliseBars(bars);
  return mapSection(song, sectionId, (section) => ({
    ...section,
    bars: nextBars,
    // A chord event past the new end is dropped; the held chord before it
    // still covers the shortened tail.
    chords: normaliseChords(
      section.chords.filter((chord) => chord.bar < nextBars),
      nextBars,
    ),
  }));
}

export function setChordAtBar(song, sectionId, bar, chord) {
  const target = song?.sections?.find((section) => section.id === sectionId);
  if (!target) return song;
  const index = Math.trunc(Number(bar));
  if (!Number.isFinite(index) || index < 0 || index >= target.bars) return song;
  const symbol = trimmedTo(chord?.symbol, MAX_SYMBOL);
  if (!symbol) return clearChordAtBar(song, sectionId, bar);
  return mapSection(song, sectionId, (section) => ({
    ...section,
    chords: normaliseChords(
      [
        ...section.chords.filter((entry) => entry.bar !== index),
        { bar: index, symbol, name: trimmedTo(chord?.name, MAX_CHORD_NAME) },
      ],
      section.bars,
    ),
  }));
}

export function clearChordAtBar(song, sectionId, bar) {
  const index = Math.trunc(Number(bar));
  return mapSection(song, sectionId, (section) => ({
    ...section,
    chords: section.chords.filter((entry) => entry.bar !== index),
  }));
}

// --- Chord suggestions ----------------------------------------------------

function catalogByRoman(song) {
  const byRoman = new Map();
  for (const chord of buildChordCatalog(song.keyRoot, song.keyScale)) {
    const existing = byRoman.get(chord.roman);
    // Prefer the diatonic spelling when a borrowed chord shares the roman.
    if (!existing || (chord.group === "diatonic" && existing.group !== "diatonic"))
      byRoman.set(chord.roman, chord);
  }
  return byRoman;
}

function placeDegrees(byRoman, bars, degrees, twelveBar = false) {
  const events = [];
  degrees.forEach((roman, index) => {
    const chord = byRoman.get(roman);
    if (!chord) return;
    const bar = twelveBar
      ? index
      : Math.floor((index * bars) / Math.max(1, degrees.length));
    if (bar < 0 || bar >= bars) return;
    events.push({ bar, symbol: chord.symbol, name: chord.name });
  });
  return normaliseChords(events, bars);
}

export function chordTemplates(keyScale = "major") {
  const progressions = keyScale === "minor" ? MINOR_PROGRESSIONS : MAJOR_PROGRESSIONS;
  return [
    ...progressions.map((progression) => ({ id: progression.id, name: progression.name })),
    { id: TWELVE_BAR.id, name: TWELVE_BAR.name },
  ];
}

export function suggestChords(song, sectionId, templateId) {
  const section = song?.sections?.find((entry) => entry.id === sectionId);
  if (!section) return song;

  const byRoman = catalogByRoman(song);
  const minor = song.keyScale === "minor";

  if (templateId === TWELVE_BAR.id) {
    return mapSection(song, sectionId, (entry) => ({
      ...entry,
      chords: placeDegrees(byRoman, entry.bars, minor ? TWELVE_BAR.minor : TWELVE_BAR.major, true),
    }));
  }

  const progressions = minor ? MINOR_PROGRESSIONS : MAJOR_PROGRESSIONS;
  const progression = progressions.find((entry) => entry.id === templateId);
  if (!progression) return song;

  return mapSection(song, sectionId, (entry) => ({
    ...entry,
    chords: placeDegrees(byRoman, entry.bars, progression.degrees),
  }));
}

// --- Templates ------------------------------------------------------------

function templateSections(templateId) {
  if (templateId === "pop") return POP_SECTIONS;
  if (templateId === "blues") return BLUES_SECTIONS;
  return [{ name: "Section", bars: 8, major: [], minor: [] }];
}

export function createSong({
  name = "New song",
  keyRoot = 0,
  keyScale = "major",
  bpm = 120,
  numerator = 4,
  denominator = 4,
  template = "blank",
} = {}) {
  const base = normaliseSong({
    id: newSongId(),
    name,
    keyRoot,
    keyScale,
    bpm,
    numerator,
    denominator,
    sections: [],
  });

  const templateId = SONG_TEMPLATES.some((entry) => entry.id === template) ? template : "blank";
  const byRoman = catalogByRoman(base);
  const minor = base.keyScale === "minor";

  const sections = templateSections(templateId).map((spec, index) => {
    const degrees = spec.twelveBar
      ? (minor ? TWELVE_BAR.minor : TWELVE_BAR.major)
      : (minor ? spec.minor : spec.major);
    return {
      id: `s${index + 1}`,
      name: spec.name,
      bars: spec.bars,
      chords: placeDegrees(byRoman, spec.bars, degrees ?? [], Boolean(spec.twelveBar)),
      idea: "",
      snippetId: 0,
      recordedBpm: 0,
      recordedBars: 0,
    };
  });

  return withSections(base, sections);
}

// --- Chart ----------------------------------------------------------------

export function sectionChordLine(section) {
  const byBar = new Map((section?.chords ?? []).map((chord) => [chord.bar, chord.symbol]));
  const cells = [];
  let held = "";
  for (let bar = 0; bar < (section?.bars ?? 0); bar += 1) {
    if (byBar.has(bar)) held = byBar.get(bar);
    cells.push(held || "-");
  }
  return cells.join(" | ");
}

function findSnippet(snippetsById, id) {
  if (!id) return null;
  if (Array.isArray(snippetsById)) return snippetsById.find((snippet) => snippet.id === id) ?? null;
  return snippetsById?.[id] ?? null;
}

export function chartText(song, snippetsById = {}) {
  if (!song) return "";

  const lines = [
    `# ${song.name}`,
    `${rootName(song.keyRoot, song.keyScale)} ${song.keyScale} - ${Math.round(song.bpm)} BPM`
      + ` - ${song.numerator}/${song.denominator}`
      + ` - ${totalBars(song)} bars`
      + ` - ~${formatSongDuration(songDurationSeconds(song))}`,
    "",
  ];

  for (const section of song.sections) {
    lines.push(`## ${section.name} (${section.bars} bars)`);
    if (section.chords.length > 0)
      lines.push(sectionChordLine(section));
    if (section.idea.trim())
      lines.push(`Idea: ${section.idea.trim()}`);
    if (section.snippetId > 0) {
      const snippet = findSnippet(snippetsById, section.snippetId);
      lines.push(`Recording: ${snippet ? snippet.name : "(missing)"}`);
    }
    lines.push("");
  }

  return lines.join("\n").trimEnd();
}

export function snippetIdsInOrder(song) {
  const seen = new Set();
  const ids = [];
  for (const section of song?.sections ?? []) {
    const id = Number(section.snippetId);
    if (id > 0 && !seen.has(id)) {
      seen.add(id);
      ids.push(id);
    }
  }
  return ids;
}

// A small tone key for the arrangement blocks. CSS maps each key to an
// existing surface token; unknown names fall back to "other".
export function sectionTone(name) {
  const lower = String(name ?? "").trim().toLowerCase();
  for (const key of ["intro", "verse", "chorus", "bridge", "solo", "outro"]) {
    if (lower.startsWith(key)) return key;
  }
  return "other";
}
