import { useEffect, useMemo, useRef, useState } from "react";
import {
  SCALES,
  STANDARD_TUNING,
  mod12,
  prefersFlats,
  rootName,
  rootNames,
  scaleById,
  scaleNotes,
  spellPitchClass,
} from "../utils";
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
} from "../chords";

const WHITE_KEYS = [
  { pc: 0, label: "C" },
  { pc: 2, label: "D" },
  { pc: 4, label: "E" },
  { pc: 5, label: "F" },
  { pc: 7, label: "G" },
  { pc: 9, label: "A" },
  { pc: 11, label: "B" },
];

const BLACK_KEYS = [
  { pc: 1, after: 0 },
  { pc: 3, after: 1 },
  { pc: 6, after: 3 },
  { pc: 8, after: 4 },
  { pc: 10, after: 5 },
];

const FRETS = 24;
const FRET_CELL_MIN = 26;
const FRET_LABEL_WIDTH = 24;
const FRET_GAP = 2;
const FRETBOARD_MIN_WIDTH = FRET_LABEL_WIDTH + (FRETS + 1) * (FRET_CELL_MIN + FRET_GAP);
const WHITE_WIDTH = 100 / WHITE_KEYS.length;

function sortedPcs(pcs) {
  return [...new Set(pcs.map(mod12))].sort((a, b) => a - b);
}

// Header chord scratchpad (0058). Frontend-only: pick a key and scale, toggle
// pitch classes on a one-octave piano or a standard-tuning fretboard, and
// choose from ranked diatonic/borrowed chord suggestions. The component never
// emits a backend event; App owns the session state and the header-tool
// ownership (Chords is a non-analysis tool beside Tuner/Scales).
export function Chords({
  root = 0,
  scale = "major",
  sevenths = false,
  selectedPcs = [],
  view = "chords",
  previewScale = null,
  chordSet = [],
  onChange,
  onClose,
}) {
  const popoverRef = useRef(null);
  const isScaleMode = view === "scales";
  const previewName = previewScale
    ? `${rootName(previewScale.root, previewScale.scaleId)} ${scaleById(previewScale.scaleId).label}`
    : "";
  // Spelling/identification follows the preview scale only inside Find scales;
  // the chord catalogue always follows the working key/scale.
  const analysisRoot = isScaleMode && previewScale ? previewScale.root : root;
  const analysisScale = isScaleMode && previewScale ? previewScale.scaleId : scale;
  // The scale drawn and highlighted on the instruments. A selected chord-set
  // scale previews on top of the working key in Chords mode too.
  const highlightRoot = previewScale ? previewScale.root : root;
  const highlightScale = previewScale ? previewScale.scaleId : scale;
  const scaleDef = scaleById(highlightScale);
  const scaleNoteList = useMemo(() => scaleNotes(highlightRoot, highlightScale), [highlightRoot, highlightScale]);
  const scalePcSet = useMemo(() => {
    if (isScaleMode && !previewScale) return new Set();
    return new Set(scaleNoteList.map((note) => note.pc));
  }, [isScaleMode, previewScale, scaleNoteList]);
  const scaleNameByPc = useMemo(() => new Map(scaleNoteList.map((note) => [note.pc, note.name])), [scaleNoteList]);
  const flats = prefersFlats(highlightRoot, parentScaleId(highlightScale));
  const noteName = (pc) => scaleNameByPc.get(mod12(pc)) ?? spellPitchClass(pc, flats);
  const keyLabel = parentKeyLabel(root, scale);
  const selected = useMemo(() => sortedPcs(selectedPcs), [selectedPcs]);
  const selectedSet = useMemo(() => new Set(selected), [selected]);
  const collectedChords = useMemo(
    () => (Array.isArray(chordSet) ? chordSet : []),
    [chordSet],
  );
  const collectedKeys = useMemo(
    () => new Set(collectedChords.map((chord) => chordSetKey(chord))),
    [collectedChords],
  );

  const catalog = useMemo(
    () => (isScaleMode ? [] : buildChordCatalog(root, scale, { sevenths })),
    [isScaleMode, root, scale, sevenths],
  );
  const ranked = useMemo(
    () => (isScaleMode ? [] : rankChords(catalog, selected)),
    [isScaleMode, catalog, selected],
  );
  const identified = useMemo(
    () => identifyChord(selected, { rootPc: analysisRoot, scaleId: analysisScale }),
    [selected, analysisRoot, analysisScale],
  );
  const selectedNames = identified?.notes ?? spellSelectedNotes(selected, analysisRoot, analysisScale);
  const identifiedInSet = Boolean(identified && collectedKeys.has(chordSetKey(identified)));

  const chordFitScales = useMemo(
    () => scalesForChords(collectedChords),
    [collectedChords],
  );

  const scaleMatches = useMemo(
    () => (isScaleMode ? findScaleMatches(selected) : []),
    [isScaleMode, selected],
  );
  const fullScaleMatches = scaleMatches.filter((match) => match.containsAll);
  const partialScaleMatches = scaleMatches.filter((match) => !match.containsAll);
  const closestScaleMatches = partialScaleMatches.filter(
    (match) => match.matched === selected.length - 1,
  );
  const scaleSuggestionPool = fullScaleMatches.length
    ? fullScaleMatches
    : closestScaleMatches.length
      ? closestScaleMatches
      : partialScaleMatches;

  const diatonic = ranked.filter((chord) => chord.group === "diatonic");
  const borrowed = ranked.filter((chord) => chord.group === "borrowed");
  const highlightedName = previewScale ? previewName : keyLabel;
  const fretboardLabel = isScaleMode && !previewScale
    ? "Standard-tuning guitar fretboard, selected notes marked"
    : `Standard-tuning guitar fretboard, notes in the ${highlightedName} scale highlighted`;

  useEffect(() => {
    popoverRef.current?.focus();
  }, []);

  const fretScrollRef = useRef(null);
  const [fretScroll, setFretScroll] = useState({ left: false, right: false });

  const updateFretScroll = () => {
    const element = fretScrollRef.current;
    if (!element) return;
    const next = {
      left: element.scrollLeft > 2,
      right: element.scrollLeft + element.clientWidth < element.scrollWidth - 2,
    };
    setFretScroll((prev) => (
      prev.left === next.left && prev.right === next.right ? prev : next
    ));
  };

  useEffect(() => {
    const element = fretScrollRef.current;
    if (!element) return undefined;
    const frame = requestAnimationFrame(updateFretScroll);
    const observer = typeof ResizeObserver !== "undefined"
      ? new ResizeObserver(updateFretScroll)
      : null;
    observer?.observe(element);
    return () => {
      cancelAnimationFrame(frame);
      observer?.disconnect();
    };
    // The fretboard width is fixed by the popover layout; re-measure only on
    // mount and when the viewport itself resizes.
  }, []);

  const scrollFrets = (direction) => {
    const element = fretScrollRef.current;
    if (!element) return;
    const amount = Math.max(120, Math.round(element.clientWidth * 0.7));
    element.scrollBy({ left: direction * amount, behavior: "smooth" });
  };

  const handleFretKeyDown = (event) => {
    const element = fretScrollRef.current;
    if (!element) return;
    if (event.key === "ArrowLeft" || event.key === "ArrowRight") {
      event.preventDefault();
      scrollFrets(event.key === "ArrowLeft" ? -1 : 1);
    } else if (event.key === "Home") {
      event.preventDefault();
      element.scrollTo({ left: 0, behavior: "smooth" });
    } else if (event.key === "End") {
      event.preventDefault();
      element.scrollTo({ left: element.scrollWidth, behavior: "smooth" });
    }
  };

  const togglePc = (pc) => {
    const value = mod12(pc);
    onChange?.({
      selectedPcs: selectedSet.has(value)
        ? selected.filter((selectedPc) => selectedPc !== value)
        : sortedPcs([...selected, value]),
    });
  };

  const chooseChord = (chord) => {
    onChange?.({ selectedPcs: sortedPcs(chord.pcs) });
  };

  const chooseRoot = (value) => onChange?.({ root: Number(value) });
  const chooseScale = (value) => onChange?.({ scale: value });
  const chooseView = (nextView) => onChange?.({ view: nextView });
  const chooseScaleSuggestion = (match) => {
    const same = previewScale
      && previewScale.root === match.rootPc
      && previewScale.scaleId === match.scaleId;
    onChange?.({
      previewScale: same ? null : { root: match.rootPc, scaleId: match.scaleId },
    });
  };

  const noteFlags = (pc) => ({
    inScale: scalePcSet.has(pc),
    root: pc === mod12(root),
    selected: selectedSet.has(pc),
  });

  const noteState = (pc) => {
    const flags = noteFlags(pc);
    const classes = ["chords-note"];
    if (flags.inScale) classes.push("is-in-scale");
    if (flags.root) classes.push("is-root");
    if (flags.selected) classes.push("is-selected");
    return classes.join(" ");
  };

  const noteAria = (pc, extra = "") => {
    const flags = noteFlags(pc);
    const parts = [noteName(pc)];
    if (flags.inScale) parts.push("in scale");
    if (flags.root) parts.push("root");
    if (flags.selected) parts.push("selected");
    if (extra) parts.push(extra);
    return parts.join(", ");
  };

  const clearSelection = () => onChange?.({
    selectedPcs: [],
    ...(previewScale ? { previewScale: null } : {}),
  });

  const toggleChordSet = (chord) => {
    const key = chordSetKey(chord);
    onChange?.({
      chordSet: collectedKeys.has(key)
        ? collectedChords.filter((item) => chordSetKey(item) !== key)
        : [...collectedChords, {
          id: chord.id ?? key,
          symbol: chord.symbol,
          name: chord.name,
          pcs: sortedPcs(chord.pcs),
          roman: chord.roman ?? "",
          label: chord.label ?? "",
          group: chord.group ?? "",
        }],
    });
  };

  const inspectChord = (chord) => onChange?.({ selectedPcs: sortedPcs(chord.pcs) });
  const chordNotes = (chord) => spellSelectedNotes(chord.pcs, analysisRoot, analysisScale);

  const suggestion = (chord) => {
    const isMatch = chord.containsAll;
    const inSet = collectedKeys.has(chordSetKey(chord));
    return (
      <div
        key={chord.id}
        className={`chords-suggestion is-chord ${isMatch ? "is-match" : ""} ${inSet ? "is-in-set" : ""}`.trim()}
      >
        <button
          type="button"
          className="chords-suggestion-load"
          onClick={() => chooseChord(chord)}
          title={`${chord.name} — load these notes${chord.label ? ` (${chord.label})` : ""}`}
          aria-label={`Inspect ${chord.roman}, ${chord.name}, notes ${chord.notes.join(" ")}${chord.label ? `, ${chord.label}` : ""}`}
        >
          <span className="chords-suggestion-roman">{chord.roman}</span>
          <span className="chords-suggestion-main">
            <span className="chords-suggestion-name">{chord.symbol}</span>
            <span className="chords-suggestion-notes">{chord.notes.join(" ")}</span>
          </span>
          {chord.label ? <span className="chords-suggestion-tag">{chord.label}</span> : null}
          {isMatch ? <span className="chords-suggestion-match">{chord.matched}/{selected.length}</span> : null}
        </button>
        <button
          type="button"
          className={`chords-suggestion-add ${inSet ? "is-on" : ""}`.trim()}
          onClick={() => toggleChordSet(chord)}
          aria-pressed={inSet}
          aria-label={inSet ? `Remove ${chord.name} from the chord set` : `Add ${chord.name} to the chord set`}
          title={inSet ? "In the chord set — click to remove" : "Add to the chord set"}
        >
          {inSet ? "✓" : "+"}
        </button>
      </div>
    );
  };

  const scaleSuggestion = (match) => {
    const isPreview = Boolean(
      previewScale
      && previewScale.root === match.rootPc
      && previewScale.scaleId === match.scaleId,
    );
    return (
      <button
        key={match.id}
        type="button"
        className={`chords-suggestion is-scale ${isPreview ? "is-preview" : ""}`.trim()}
        onClick={() => chooseScaleSuggestion(match)}
        title={`${match.name}: ${match.notes.join(" ")}`}
        aria-label={`${match.name}, notes ${match.notes.join(" ")}, ${match.matched} of ${match.selectedCount} selected notes${match.containsAll ? ", contains all selected notes" : ""}`}
        aria-pressed={isPreview}
      >
        <span className="chords-suggestion-roman">
          {match.containsAll ? "✓" : `${match.matched}/${match.selectedCount}`}
        </span>
        <span className="chords-suggestion-main">
          <span className="chords-suggestion-name">{match.name}</span>
          <span className="chords-suggestion-notes">{match.notes.join(" ")}</span>
        </span>
        <span className="chords-suggestion-tag">
          {match.containsAll ? "all notes" : `${match.missing} missing`}
        </span>
      </button>
    );
  };

  const scaleChip = (match) => {
    const isPreview = Boolean(
      previewScale
      && previewScale.root === match.rootPc
      && previewScale.scaleId === match.scaleId,
    );
    return (
      <button
        key={match.id}
        type="button"
        className={`chords-scale-chip ${isPreview ? "is-preview" : ""}`.trim()}
        onClick={() => chooseScaleSuggestion(match)}
        aria-pressed={isPreview}
        title={`${match.name}: ${match.notes.join(" ")}`}
      >
        {match.name}
      </button>
    );
  };

  return (
    <div
      className="chords-popover"
      role="dialog"
      aria-label="Chord scratchpad"
      tabIndex={-1}
      ref={popoverRef}
      onKeyDown={(event) => {
        if (event.key === "Escape") {
          event.stopPropagation();
          if (onClose) onClose();
        }
      }}
    >
      <div className="chords-popover-header">
        <div className="chords-heading">
          <span className="chords-popover-title">{isScaleMode ? "Find scales" : "Chords"}</span>
          <span className="chords-key-context">
            {previewName || (isScaleMode ? "Pick notes to find scales" : keyLabel)}
          </span>
        </div>

        <div className="chords-mode chords-mode-switch" role="group" aria-label="Chords tool mode">
          <button
            type="button"
            className={`chords-mode-btn ${!isScaleMode ? "is-on" : ""}`.trim()}
            aria-pressed={!isScaleMode}
            onClick={() => chooseView("chords")}
          >
            Chords
          </button>
          <button
            type="button"
            className={`chords-mode-btn ${isScaleMode ? "is-on" : ""}`.trim()}
            aria-pressed={isScaleMode}
            onClick={() => chooseView("scales")}
          >
            Find scales
          </button>
        </div>

        <div className="chords-header-controls">
          {previewScale ? (
            <button
              type="button"
              className="chords-clear-preview"
              onClick={() => onChange?.({ previewScale: null })}
            >
              Clear scale
            </button>
          ) : null}

          {isScaleMode ? null : (
            <>
              <label className="chords-field">
                <span className="chords-field-label">Key</span>
                <select
                  className="chords-select"
                  value={mod12(root)}
                  onChange={(event) => chooseRoot(event.target.value)}
                  aria-label="Chord key"
                >
                  {rootNames(scale).map((name, pc) => <option key={pc} value={pc}>{name}</option>)}
                </select>
              </label>

              <label className="chords-field">
                <span className="chords-field-label">Scale</span>
                <select
                  className="chords-select"
                  value={scale}
                  onChange={(event) => chooseScale(event.target.value)}
                  aria-label="Chord scale"
                >
                  {SCALES.map((item) => <option key={item.id} value={item.id}>{item.label}</option>)}
                </select>
              </label>

              <div className="chords-mode" role="group" aria-label="Chord complexity">
                <button
                  type="button"
                  className={`chords-mode-btn ${!sevenths ? "is-on" : ""}`.trim()}
                  aria-pressed={!sevenths}
                  onClick={() => onChange?.({ sevenths: false })}
                >
                  Triads
                </button>
                <button
                  type="button"
                  className={`chords-mode-btn ${sevenths ? "is-on" : ""}`.trim()}
                  aria-pressed={sevenths}
                  onClick={() => onChange?.({ sevenths: true })}
                >
                  7ths
                </button>
              </div>
            </>
          )}
        </div>
      </div>

      {isScaleMode && collectedChords.length === 0 ? null : (
        <section className="chords-set" aria-label="Chord set">
          <div className="chords-set-head">
            <span className="chords-kicker">Chord set</span>
            <span className="chords-set-count">
              {collectedChords.length
                ? `${collectedChords.length} selected`
                : "empty · add chords with +"}
            </span>
            {collectedChords.length ? (
              <button
                type="button"
                className="chords-set-clear"
                onClick={() => onChange?.({ chordSet: [] })}
              >
                Clear set
              </button>
            ) : null}
          </div>

          {collectedChords.length ? (
            <>
              <div className="chords-chip-row">
                {collectedChords.map((chord) => {
                  const key = chordSetKey(chord);
                  const notes = chordNotes(chord).join(" ");
                  return (
                    <div className="chords-chip" key={key}>
                      <button
                        type="button"
                        className="chords-chip-main"
                        onClick={() => inspectChord(chord)}
                        title={`Inspect ${chord.name} — ${notes}`}
                        aria-label={`Inspect ${chord.name}, notes ${notes}`}
                      >
                        <span className="chords-chip-symbol">{chord.symbol}</span>
                        <span className="chords-chip-notes">{notes}</span>
                      </button>
                      <button
                        type="button"
                        className="chords-chip-remove"
                        onClick={() => toggleChordSet(chord)}
                        aria-label={`Remove ${chord.name} from the chord set`}
                        title="Remove from the chord set"
                      >
                        ×
                      </button>
                    </div>
                  );
                })}
              </div>

              <div className="chords-fit-row">
                <span className="chords-fit-label">
                  {chordFitScales.length
                    ? `Scales fitting all ${collectedChords.length}`
                    : "Scales fitting all"}
                </span>
                {chordFitScales.length ? (
                  <div className="chords-scale-chips">{chordFitScales.map(scaleChip)}</div>
                ) : (
                  <span className="chords-set-empty">
                    No single scale contains every tone of these chords.
                  </span>
                )}
              </div>
            </>
          ) : (
            <p className="chords-set-hint">
              Click <strong>+</strong> on a chord — or “Add to set” under Detected chord — to collect it.
              Every scale that contains all of them is listed here.
            </p>
          )}
        </section>
      )}

      <div className="chords-body">
        <aside
          className="chords-suggestions"
          aria-label={isScaleMode ? "Scale suggestions" : "Chord suggestions"}
        >
          <div className={`chords-detected-card ${identified ? "has-chord" : ""}`.trim()} aria-live="polite">
            <div className="chords-detected-head">
              <span className="chords-kicker">{isScaleMode ? "Selected notes" : "Detected chord"}</span>
              <div className="chords-detected-actions">
                {isScaleMode ? null : (
                  <button
                    type="button"
                    className={`chords-detected-add ${identifiedInSet ? "is-on" : ""}`.trim()}
                    onClick={() => identified && toggleChordSet(identified)}
                    disabled={!identified}
                    aria-pressed={identifiedInSet}
                    title={identified
                      ? (identifiedInSet ? "Already in the chord set" : "Add this chord to the chord set")
                      : "Select three or four notes to detect a chord"}
                  >
                    {identifiedInSet ? "In set" : "Add to set"}
                  </button>
                )}
                <button
                  type="button"
                  className="chords-clear"
                  onClick={clearSelection}
                  disabled={selected.length === 0}
                >
                  Clear
                </button>
              </div>
            </div>
            <div className="chords-detected-main">
              <span className={`chords-detected-symbol ${identified ? "" : "is-muted"}`.trim()}>
                {identified ? identified.symbol : selected.length >= 3 ? "?" : "–"}
              </span>
              <span className="chords-detected-name">
                {identified
                  ? identified.name
                  : selected.length >= 3
                    ? "Unrecognised shape"
                    : selected.length
                      ? `${selected.length} notes`
                      : "Pick notes"}
              </span>
            </div>
            <div className="chords-detected-notes">
              {selected.length ? selectedNames.join("  ") : "Tap the piano or fretboard to begin"}
            </div>
          </div>

          <p className="chords-hint">
            {isScaleMode
              ? selected.length < 2
                ? "Pick at least 2 notes to see scales that contain them"
                : fullScaleMatches.length
                  ? `${fullScaleMatches.length} scale${fullScaleMatches.length === 1 ? "" : "s"} contain all ${selected.length} notes · choose one to highlight it`
                  : `${scaleSuggestionPool.length} closest scale${scaleSuggestionPool.length === 1 ? "" : "s"} · no scale contains every note`
              : selected.length
                ? `${ranked.length} compatible chord${ranked.length === 1 ? "" : "s"} · click a row to inspect, + to collect`
                : `Chords in ${keyLabel} · scale tones highlighted`}
          </p>

          <div className="chords-suggestion-scroll is-scroll">
            {isScaleMode ? (
              selected.length < 2 ? (
                <p className="chords-empty">Select two or more notes on the piano or fretboard.</p>
              ) : scaleSuggestionPool.length === 0 ? (
                <p className="chords-empty">No scale contains these notes. Try removing one.</p>
              ) : fullScaleMatches.length ? (
                <section className="chords-group" aria-label="Scales containing all selected notes">
                  <h3 className="chords-group-title">
                    Contains all notes <span>{fullScaleMatches.length}</span>
                  </h3>
                  <div className="chords-suggestion-list">{scaleSuggestionPool.map(scaleSuggestion)}</div>
                </section>
              ) : (
                <section className="chords-group" aria-label="Closest scale fits">
                  <h3 className="chords-group-title">
                    Closest fits <span>{scaleSuggestionPool.length} scales</span>
                  </h3>
                  <div className="chords-suggestion-list">{scaleSuggestionPool.map(scaleSuggestion)}</div>
                </section>
              )
            ) : (
              <>
                <section className="chords-group" aria-label="Chords in key">
                  <h3 className="chords-group-title">
                    In key <span>{keyLabel}</span>
                  </h3>
                  <div className="chords-suggestion-list">{diatonic.map(suggestion)}</div>
                </section>

                <section className="chords-group" aria-label="Borrowed chords">
                  <h3 className="chords-group-title">
                    Borrowed <span>colour options</span>
                  </h3>
                  <div className="chords-suggestion-list">{borrowed.map(suggestion)}</div>
                </section>
              </>
            )}
          </div>
        </aside>

        <div className="chords-instruments">
          <div className="chords-instrument-head">
            <span className="chords-instrument-title">Piano</span>
            <span className="chords-instrument-hint">One octave · click to toggle</span>
          </div>
          <div className="chords-piano" role="group" aria-label="One-octave piano">
            <div className="chords-piano-whites">
              {WHITE_KEYS.map((key) => (
                <button
                  key={key.pc}
                  type="button"
                  className={`${noteState(key.pc)} is-white`.trim()}
                  aria-label={noteAria(key.pc)}
                  aria-pressed={selectedSet.has(key.pc)}
                  title={`${noteName(key.pc)}${scalePcSet.has(key.pc) ? " · in scale" : ""}`}
                  onClick={() => togglePc(key.pc)}
                >
                  <span className="chords-note-label">{noteName(key.pc)}</span>
                </button>
              ))}
            </div>
            <div className="chords-piano-blacks">
              {BLACK_KEYS.map((key) => (
                <button
                  key={key.pc}
                  type="button"
                  className={`${noteState(key.pc)} is-black`.trim()}
                  style={{ left: `${(key.after + 1) * WHITE_WIDTH}%` }}
                  aria-label={noteAria(key.pc)}
                  aria-pressed={selectedSet.has(key.pc)}
                  title={`${noteName(key.pc)}${scalePcSet.has(key.pc) ? " · in scale" : ""}`}
                  onClick={() => togglePc(key.pc)}
                >
                  <span className="chords-note-label">{noteName(key.pc)}</span>
                </button>
              ))}
            </div>
          </div>

          <div className="chords-instrument-head chords-fretboard-head">
            <span className="chords-instrument-title">Guitar</span>
            <span className="chords-instrument-hint">Standard tuning · 0–{FRETS} frets</span>
            <div className="chords-fretboard-nav" role="group" aria-label="Fretboard scroll">
              <button
                type="button"
                className="chords-scroll-btn"
                onClick={() => scrollFrets(-1)}
                disabled={!fretScroll.left}
                aria-label="Scroll fretboard left"
              >
                ‹
              </button>
              <button
                type="button"
                className="chords-scroll-btn"
                onClick={() => scrollFrets(1)}
                disabled={!fretScroll.right}
                aria-label="Scroll fretboard right"
              >
                ›
              </button>
            </div>
          </div>
          <div
            className="chords-fretboard-scroll"
            ref={fretScrollRef}
            role="region"
            tabIndex={0}
            onScroll={updateFretScroll}
            onKeyDown={handleFretKeyDown}
            aria-label="Guitar fretboard. Use the arrow keys, Home, End or the scroll buttons to move across the frets."
          >
            <div
              className="chords-fretboard"
              style={{ "--fret-count": String(FRETS + 1), minWidth: `${FRETBOARD_MIN_WIDTH}px` }}
              role="group"
              aria-label={fretboardLabel}
            >
              <div className="chords-fret-numbers" aria-hidden="true">
                <span className="chords-string-name" />
                {Array.from({ length: FRETS + 1 }, (_, fret) => (
                  <span key={fret} className="chords-fret-number">{fret}</span>
                ))}
              </div>
              {[...STANDARD_TUNING].reverse().map((string) => (
                <div className="chords-string" key={string.midi}>
                  <span className="chords-string-name">{string.label}</span>
                  {Array.from({ length: FRETS + 1 }, (_, fret) => {
                    const midi = string.midi + fret;
                    const pc = mod12(midi);
                    return (
                      <button
                        key={fret}
                        type="button"
                        className={noteState(pc)}
                        aria-label={noteAria(pc, `string ${string.label}, fret ${fret}`)}
                        aria-pressed={selectedSet.has(pc)}
                        title={`${noteName(pc)} · string ${string.label}, fret ${fret}`}
                        onClick={() => togglePc(pc)}
                      >
                        <span className="chords-fret-note">{noteName(pc)}</span>
                      </button>
                    );
                  })}
                </div>
              ))}
            </div>
          </div>
          {isScaleMode && !previewScale ? (
            <p className="chords-scale-summary is-empty">
              Choose a suggested scale to highlight its notes.
            </p>
          ) : (
            <p className="chords-scale-summary">
              <span className="chords-scale-root">{scaleNoteList[0]?.name}</span>
              {" "}
              {scaleDef.label}: {scaleNoteList.map((note) => note.name).join(" ")}
            </p>
          )}
        </div>
      </div>
    </div>
  );
}
