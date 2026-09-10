import { useEffect, useRef, useState } from "react";
import { Waveform } from "./Waveform";
import { Knob } from "./controls";
import { formatDate, formatTime, SNIPPET_COLORS, snippetColor } from "../utils";
import { FRONTEND_EVENTS, emit } from "../bridge";
import {
  IconAnalyze,
  IconChevronDown,
  IconChevronUp,
  IconExternal,
  IconPlay,
  IconSave,
  IconStop,
  IconTrash,
  IconX,
} from "./icons";

export function SnippetCard({ snippet, tagNames, isPlaying, playPositionSeconds }) {
  const [name, setName] = useState(snippet.name ?? "");
  const [comments, setComments] = useState(snippet.comments ?? "");
  const [color, setColor] = useState(snippet.color ?? "");
  const [gainDb, setGainDb] = useState(Number(snippet.gainDb ?? 0));
  const [expanded, setExpanded] = useState(false);
  // `detecting` flips on while an analysis request is in flight
  // and clears when the backend echoes the (possibly empty) result
  // back through the snippet prop. We key it on the snippet id so
  // switching cards doesn't carry the spinner across.
  const [detecting, setDetecting] = useState(false);
  // Two-step delete: the first click arms the button ("Confirm?"), the
  // second commits. Arming auto-disarms after a few seconds.
  const [confirmDelete, setConfirmDelete] = useState(false);

  // Track the last values we successfully committed so we never
  // skip a commit because the backend echoed the same prop values
  // back before the user finished editing.
  const lastCommitted = useRef({ name: snippet.name ?? "", comments: snippet.comments ?? "" });

  // Only resync from props when this card switches to a different snippet.
  // We deliberately don't resync on every prop change so a user who is
  // actively editing name/comments doesn't get their in-progress text
  // clobbered when the backend echoes the previous value back.
  const lastIdRef = useRef(snippet.id);
  useEffect(() => {
    if (lastIdRef.current !== snippet.id) {
      lastIdRef.current = snippet.id;
      setName(snippet.name ?? "");
      setComments(snippet.comments ?? "");
      setColor(snippet.color ?? "");
      setExpanded(false);
      setDetecting(false);
      lastCommitted.current = { name: snippet.name ?? "", comments: snippet.comments ?? "" };
    }
  }, [snippet.id, snippet.name, snippet.comments]);

  // The backend echoes the persisted trim (including after Normalize)
  // through the snippet prop; mirror it. During a drag the prop does not
  // change until the debounced flush, so this never fights the knob.
  useEffect(() => {
    setGainDb(Number(snippet.gainDb ?? 0));
  }, [snippet.gainDb, snippet.id]);

  const handleColor = (next) => {
    setColor(next);
    emit(FRONTEND_EVENTS.setSnippetColor, { id: snippet.id, color: next });
  };

  const handleGain = (next) => {
    setGainDb(next);
    emit(FRONTEND_EVENTS.setSnippetGain, { id: snippet.id, gainDb: next });
  };

  const handleNormalize = () => {
    emit(FRONTEND_EVENTS.normalizeSnippet, { id: snippet.id });
  };

  // The backend clears `key` immediately on a fresh detect request,
  // so the prop changing from "C major" -> "" signals that detection
  // is running. When it changes back to a value (or stays empty
  // because the detector found nothing) the spinner clears. We also
  // key on `notes` so re-runs that change the note list drop the
  // spinner the same way.
  useEffect(() => {
    setDetecting(false);
  }, [snippet.key, snippet.notes]);

  const commitMeta = () => {
    if (name === lastCommitted.current.name && comments === lastCommitted.current.comments) return;
    emit(FRONTEND_EVENTS.updateSnippet, {
      id: snippet.id,
      name,
      comments,
    });
    lastCommitted.current = { name, comments };
  };

  const handlePlay = () => {
    if (isPlaying) emit(FRONTEND_EVENTS.stopPlayback);
    else emit(FRONTEND_EVENTS.startPlayback, { id: snippet.id });
  };

  useEffect(() => {
    if (!confirmDelete) return undefined;
    const t = setTimeout(() => setConfirmDelete(false), 3500);
    return () => clearTimeout(t);
  }, [confirmDelete]);

  const handleDelete = () => {
    if (!confirmDelete) {
      setConfirmDelete(true);
      return;
    }
    setConfirmDelete(false);
    emit(FRONTEND_EVENTS.deleteSnippet, { id: snippet.id });
  };

  const handleSave = () => {
    emit(FRONTEND_EVENTS.saveSnippet, { id: snippet.id });
  };

  const handleReveal = () => {
    emit(FRONTEND_EVENTS.revealSnippet, { id: snippet.id });
  };

  const handleDetectKey = () => {
    setDetecting(true);
    emit(FRONTEND_EVENTS.detectSnippetKey, { id: snippet.id });
  };

  const toggleExpanded = () => setExpanded((v) => !v);

  const duration = Number(snippet.durationSeconds) || 0;
  const showPosition = isPlaying ? Math.min(duration, playPositionSeconds) : 0;
  const displayName = name.trim() || "Untitled snippet";
  const positionLabel = isPlaying
    ? `${formatTime(showPosition)} / ${formatTime(duration)}`
    : formatTime(duration);
  const hasKey = typeof snippet.key === "string" && snippet.key.length > 0;
  const hasNotes = Array.isArray(snippet.notes) && snippet.notes.length > 0;
  const hasAnalysis = hasKey || hasNotes;
  const keyConfidence = Number(snippet.keyConfidence) || 0;
  const keyTitle = hasKey
    ? `Detected key: ${snippet.key} (confidence ${Math.round(keyConfidence * 100)}%)`
    : "No key detected yet";
  const currentColor = snippetColor(color);
  const currentColorName = (() => {
    const custom = tagNames?.[color];
    if (typeof custom === "string" && custom.trim()) return custom.trim();
    return currentColor?.label ?? "";
  })();

  return (
    <article
      className={`snippet ${isPlaying ? "is-playing" : ""} ${expanded ? "is-expanded" : "is-mini"}`}
    >
      <header className="snippet-header">
        <button
          type="button"
          className={`play-button ${isPlaying ? "is-playing" : ""}`}
          onClick={handlePlay}
          title={isPlaying ? "Stop playback" : "Play snippet"}
          aria-label={isPlaying ? "Stop playback" : "Play snippet"}
        >
          {isPlaying ? <IconStop size={13} /> : <IconPlay size={14} />}
        </button>
        <div
          className="snippet-summary"
          role="button"
          tabIndex={0}
          aria-expanded={expanded}
          onClick={toggleExpanded}
          onKeyDown={(e) => {
            if (e.key === "Enter" || e.key === " ") {
              e.preventDefault();
              toggleExpanded();
            }
          }}
        >
          <span className="snippet-title-line">
            {currentColor ? (
              <span
                className="snippet-color-dot"
                style={{ background: currentColor.main }}
                title={currentColorName ? `Tag: ${currentColorName}` : `Colour: ${currentColor.label}`}
                aria-hidden="true"
              />
            ) : null}
            <span className="snippet-name" title={displayName}>{displayName}</span>
          </span>
          {hasKey ? (
            <span
              className="chip chip-key"
              title={keyTitle}
              aria-label={keyTitle}
            >
              {snippet.key}
            </span>
          ) : null}
          {hasNotes ? (
            <span
              className="chip chip-notes"
              title={`Detected pitch classes: ${snippet.notes.join(", ")}`}
              aria-label={`Detected notes: ${snippet.notes.join(", ")}`}
            >
              {snippet.notes.join(" ")}
            </span>
          ) : null}
          <span className="snippet-duration">{positionLabel}</span>
          <span className="snippet-date">{formatDate(snippet.createdAt)}</span>
        </div>
        <button
          type="button"
          className="icon-btn snippet-expand-button"
          onClick={toggleExpanded}
          aria-expanded={expanded}
          aria-label={expanded ? "Collapse snippet" : "Expand snippet"}
          title={expanded ? "Collapse" : "Expand"}
        >
          {expanded ? <IconChevronUp size={14} /> : <IconChevronDown size={14} />}
        </button>
      </header>

      {expanded ? (
        <div className="snippet-details">
          <div
            className="snippet-waveform"
            style={currentColor ? { background: currentColor.soft } : undefined}
          >
            <Waveform peaks={snippet.peaks ?? []} width={520} height={56} />
            {isPlaying ? (
              <div
                className="snippet-playhead"
                style={{
                  left: duration > 0
                    ? `calc(6px + (100% - 12px) * ${Math.min(1, Math.max(0, playPositionSeconds / duration))})`
                    : "6px",
                }}
              />
            ) : null}
          </div>

          <div className="snippet-field snippet-color-field">
            <span>Colour</span>
            <div className="snippet-swatches" role="group" aria-label="Snippet colour">
              {SNIPPET_COLORS.map((c) => (
                <button
                  key={c.key}
                  type="button"
                  className={`snippet-swatch ${color === c.key ? "is-active" : ""}`}
                  style={{ background: c.main }}
                  onClick={() => handleColor(c.key)}
                  title={`${c.label} colour`}
                  aria-label={`${c.label} colour`}
                  aria-pressed={color === c.key}
                />
              ))}
              <button
                type="button"
                className={`snippet-swatch snippet-swatch-none ${!color ? "is-active" : ""}`}
                onClick={() => handleColor("")}
                title="No colour"
                aria-label="No colour"
                aria-pressed={!color}
              >
                <IconX size={10} />
              </button>
            </div>
          </div>

          <label className="snippet-field">
            <span>Name</span>
            <input
              type="text"
              value={name}
              maxLength={80}
              onChange={(e) => setName(e.target.value)}
              onBlur={commitMeta}
              onKeyDown={(e) => { if (e.key === "Enter") e.currentTarget.blur(); }}
              placeholder="Untitled snippet"
            />
          </label>

          <label className="snippet-field">
            <span>Comments</span>
            <textarea
              rows={3}
              value={comments}
              maxLength={2000}
              onChange={(e) => setComments(e.target.value)}
              onBlur={commitMeta}
              placeholder="What did you play? Tuning, take notes, what to work on…"
            />
          </label>

          <div className="snippet-field snippet-gain-field">
            <span>Gain</span>
            <div className="snippet-gain-controls">
              <Knob
                label="Gain"
                min={-24}
                max={24}
                value={gainDb}
                onChange={handleGain}
                step="0.5"
                decimals={1}
                unit="dB"
                className="snippet-gain-knob"
                title="Non-destructive playback trim for this snippet — never baked into the WAV"
              />
              <button
                type="button"
                className="btn btn-sm"
                onClick={handleNormalize}
                title="Set the trim so this snippet's peak lands at -1 dBFS"
              >
                Normalize
              </button>
            </div>
          </div>

          <footer className="snippet-footer">
            <div className="snippet-meta">
              #{snippet.id} · {Number(snippet.numChannels) || 0}ch · {Math.round(Number(snippet.sampleRate) || 0)} Hz
              {hasKey
                ? <span className="snippet-key-inline" title={keyTitle}>
                    {" "}· key <strong>{snippet.key}</strong> ({Math.round(keyConfidence * 100)}%)
                  </span>
                : <span className="snippet-key-inline snippet-key-empty"> · no key detected</span>}
              {hasNotes
                ? <span
                    className="snippet-notes-inline"
                    title={`Detected pitch classes: ${snippet.notes.join(", ")}`}
                  > · notes <strong>{snippet.notes.join(" ")}</strong></span>
                : null}
              {snippet.savedPath ? <span className="snippet-saved"> · saved</span> : null}
            </div>
            <div className="snippet-actions">
              <button
                type="button"
                className="btn btn-sm"
                onClick={handleDetectKey}
                disabled={detecting}
                title={
                  hasAnalysis
                    ? "Re-analyse the snippet (key + detected notes)"
                    : "Analyse the snippet to detect the key and notes"
                }
              >
                <IconAnalyze size={13} />
                {detecting ? "Analysing…" : hasAnalysis ? "Re-analyse" : "Analyse"}
              </button>
              <button
                type="button"
                className="btn btn-sm"
                onClick={handleSave}
                title="Save snippet to a WAV file"
              >
                <IconSave size={13} />
                Save
              </button>
              <button
                type="button"
                className="btn btn-sm"
                onClick={handleReveal}
                disabled={!snippet.savedPath}
                title="Reveal saved file in Explorer"
              >
                <IconExternal size={13} />
                Reveal
              </button>
              <button
                type="button"
                className={`btn btn-sm btn-danger ${confirmDelete ? "is-armed" : ""}`}
                onClick={handleDelete}
                title={confirmDelete
                  ? "Click again to permanently delete this snippet"
                  : "Delete snippet"}
              >
                <IconTrash size={13} />
                {confirmDelete ? "Confirm?" : "Delete"}
              </button>
            </div>
          </footer>
        </div>
      ) : null}
    </article>
  );
}
