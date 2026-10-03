import { useEffect, useMemo, useRef, useState } from "react";
import {
  SCALES,
  STANDARD_TUNING,
  lockScalePrefill,
  parseDetectedKey,
  parseNoteName,
  prefersFlats,
  rootName,
  rootNames,
  scaleById,
  scaleNotes,
  scalePrefillStep,
  spellPitchClass,
} from "../utils";

const FRETS = 12;

// Header Scales popover (0053): a root + scale selector over a standard-tuning
// fretboard. Opening it reuses the tuner's live analysis (the App emits the
// existing tuner-open event) so the note being played highlights; it never
// touches the tuner's monitor mute. The chosen root + scale persist in
// localStorage via onPersist.
export function PracticeScales({
  transport,
  playingKey = "",
  playingSourceId = -1,
  initialRoot = 0,
  initialScale = "major",
  onPersist,
  onClose,
}) {
  const [root, setRoot] = useState(initialRoot);
  const [scale, setScale] = useState(initialScale);
  const popoverRef = useRef(null);
  // Prefill state for the current open/source: which playing id the current
  // choice belongs to, and whether it has been claimed (prefilled or
  // hand-edited). The ref starts fresh on every mount, so reopening the
  // popover prefills again from whatever is playing then.
  const prefillRef = useRef({ source: undefined, locked: false });

  // Prefill from the playing snippet's detected key when a source starts (or
  // when the popover opens with one already playing). The playing id can land
  // before the snippet payload that carries its key, so a source with no key
  // yet stays unlocked and is retried on the next push. A manual choice (or a
  // completed prefill) locks that source — the key never overwrites it while
  // the same snippet plays. A new source resets the lock. This "manual choice
  // sticks" is within one open/source only: reopening the popover prefills
  // from the currently playing key, as the ticket asks.
  useEffect(() => {
    const parsed = parseDetectedKey(playingKey);
    const { state, apply } = scalePrefillStep(prefillRef.current, playingSourceId, parsed);
    prefillRef.current = state;
    if (!apply) return;
    setRoot(apply.pc);
    setScale(apply.scaleId);
    if (onPersist) onPersist(apply.pc, apply.scaleId);
  }, [playingSourceId, playingKey, onPersist]);

  // Focus the popover on open (0017 keyboard conventions).
  useEffect(() => {
    popoverRef.current?.focus();
  }, []);

  const notes = useMemo(() => scaleNotes(root, scale), [root, scale]);
  const scaleDef = scaleById(scale);
  const pcSet = useMemo(() => new Set(notes.map((n) => n.pc)), [notes]);
  const nameByPc = useMemo(() => new Map(notes.map((n) => [n.pc, n.name])), [notes]);
  const flats = prefersFlats(root, scale);

  const liveNote = parseNoteName(transport?.tunerNote);
  const liveActive = Boolean(transport?.tunerNote) && Number(transport?.tunerConfidence ?? 0) > 0.5;

  const chooseRoot = (pc) => {
    prefillRef.current = lockScalePrefill(prefillRef.current);
    setRoot(pc);
    if (onPersist) onPersist(pc, scale);
  };
  const chooseScale = (id) => {
    prefillRef.current = lockScalePrefill(prefillRef.current);
    setScale(id);
    if (onPersist) onPersist(root, id);
  };

  // Displayed high-E first, matching a real fretboard (low-E at the bottom).
  const strings = [...STANDARD_TUNING].reverse();
  const noteSummary = notes.map((n) => n.name).join(" ");
  const rootLabel = rootName(root, scale);

  return (
    <div
      className="scales-popover"
      role="dialog"
      aria-label="Practice scales"
      tabIndex={-1}
      ref={popoverRef}
      onKeyDown={(e) => {
        if (e.key === "Escape") {
          e.stopPropagation();
          if (onClose) onClose();
        }
      }}
    >
      <div className="scales-popover-header">
        <span className="scales-popover-title">Scales</span>
        <label className="scales-field">
          <span className="scales-field-label">Key</span>
          <select
            className="scales-select"
            value={root}
            onChange={(e) => chooseRoot(Number(e.target.value))}
            aria-label="Scale root"
          >
            {rootNames(scale).map((name, pc) => <option key={pc} value={pc}>{name}</option>)}
          </select>
        </label>
        <label className="scales-field">
          <span className="scales-field-label">Scale</span>
          <select
            className="scales-select"
            value={scale}
            onChange={(e) => chooseScale(e.target.value)}
            aria-label="Scale type"
          >
            {SCALES.map((s) => <option key={s.id} value={s.id}>{s.label}</option>)}
          </select>
        </label>
      </div>

      <div className="scales-notes" aria-live="polite">
        <span className="scales-notes-title">{rootLabel} {scaleDef.label}</span>
        <span className="scales-notes-list">{noteSummary}</span>
      </div>

      <div
        className="scales-fretboard"
        role="img"
        aria-label={`${rootLabel} ${scaleDef.label} scale on a standard-tuning guitar: ${noteSummary}`}
      >
        <div className="scales-fret-numbers" aria-hidden="true">
          <span className="scales-string-name" />
          {Array.from({ length: FRETS + 1 }, (_, fret) => (
            <span key={fret} className="scales-fret-number">{fret === 0 ? "" : fret}</span>
          ))}
        </div>
        {strings.map((string) => (
          <div className="scales-string" key={string.midi}>
            <span className="scales-string-name">{string.label}</span>
            {Array.from({ length: FRETS + 1 }, (_, fret) => {
              const midi = string.midi + fret;
              const pc = ((midi % 12) + 12) % 12;
              const inScale = pcSet.has(pc);
              const isRoot = pc === root;
              const isLive = liveActive && liveNote
                && (liveNote.midi != null ? liveNote.midi === midi : liveNote.pc === pc);
              const title = inScale
                ? `${nameByPc.get(pc)} — fret ${fret}`
                : `${spellPitchClass(pc, flats)} — fret ${fret}`;
              return (
                <span
                  key={fret}
                  className={`scales-fret ${inScale ? "is-in-scale" : ""} ${isRoot ? "is-root" : ""} ${isLive ? "is-live" : ""}`}
                  title={title}
                  aria-hidden="true"
                >
                  {inScale ? nameByPc.get(pc) : ""}
                </span>
              );
            })}
          </div>
        ))}
      </div>

      <p className="scales-hint">
        {liveActive && liveNote
          ? `Live: ${nameByPc.get(liveNote.pc) ?? spellPitchClass(liveNote.pc, flats)}`
          : "Play a note to highlight it"}
      </p>
    </div>
  );
}
