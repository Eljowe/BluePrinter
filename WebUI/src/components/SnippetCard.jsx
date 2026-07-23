import { useEffect, useRef, useState } from "react";
import { Waveform } from "./Waveform";
import { formatDate, formatTime } from "../utils";
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
} from "./icons";

export function SnippetCard({ snippet, isPlaying, playPositionSeconds }) {
  const [name, setName] = useState(snippet.name ?? "");
  const [comments, setComments] = useState(snippet.comments ?? "");
  const [expanded, setExpanded] = useState(false);
  // `detecting` flips on while an analysis request is in flight
  // and clears when the backend echoes the (possibly empty) result
  // back through the snippet prop. We key it on the snippet id so
  // switching cards doesn't carry the spinner across.
  const [detecting, setDetecting] = useState(false);

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
      setExpanded(false);
      setDetecting(false);
      lastCommitted.current = { name: snippet.name ?? "", comments: snippet.comments ?? "" };
    }
  }, [snippet.id, snippet.name, snippet.comments]);

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

  const handleDelete = () => {
    if (window.confirm(`Delete snippet "${snippet.name || "(untitled)"}"?`)) {
      emit(FRONTEND_EVENTS.deleteSnippet, { id: snippet.id });
    }
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
          <span className="snippet-name" title={displayName}>{displayName}</span>
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
          <div className="snippet-waveform">
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
                className="btn btn-sm btn-danger"
                onClick={handleDelete}
                title="Delete snippet"
              >
                <IconTrash size={13} />
                Delete
              </button>
            </div>
          </footer>
        </div>
      ) : null}
    </article>
  );
}
