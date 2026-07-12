import { useEffect, useRef, useState } from "react";
import { Waveform } from "./Waveform";
import { formatDate, formatTime } from "../utils";
import { FRONTEND_EVENTS, emit } from "../bridge";

export function SnippetCard({ snippet, isPlaying, playPositionSeconds }) {
  const [name, setName] = useState(snippet.name ?? "");
  const [comments, setComments] = useState(snippet.comments ?? "");
  const [expanded, setExpanded] = useState(false);
  // `detecting` flips on while a key-detection request is in flight
  // and clears when the backend echoes the (possibly empty) result
  // back through the snippet prop. We key it on the snippet id so
  // switching cards doesn't carry the spinner across.
  const [detecting, setDetecting] = useState(false);

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
    }
  }, [snippet.id, snippet.name, snippet.comments]);

  // The backend clears `key` immediately on a fresh detect request,
  // so the prop changing from "C major" -> "" signals that detection
  // is running. When it changes back to a value (or stays empty
  // because the detector found nothing) the spinner clears.
  useEffect(() => {
    setDetecting(false);
  }, [snippet.key]);

  const commitMeta = () => {
    if (name === snippet.name && comments === snippet.comments) return;
    emit(FRONTEND_EVENTS.updateSnippet, {
      id: snippet.id,
      name,
      comments,
    });
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
        >
          {isPlaying ? "■" : "▶"}
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
              className="snippet-key-badge"
              title={keyTitle}
              aria-label={keyTitle}
            >
              {snippet.key}
            </span>
          ) : null}
          <span className="snippet-sep" aria-hidden="true">·</span>
          <span className="snippet-duration">{positionLabel}</span>
          <span className="snippet-sep" aria-hidden="true">·</span>
          <span className="snippet-date">{formatDate(snippet.createdAt)}</span>
        </div>
        <button
          type="button"
          className="snippet-expand-button"
          onClick={toggleExpanded}
          aria-expanded={expanded}
          aria-label={expanded ? "Collapse snippet" : "Expand snippet"}
          title={expanded ? "Collapse" : "Expand"}
        >
          {expanded ? "▲" : "▼"}
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
                    · key: <strong>{snippet.key}</strong> ({Math.round(keyConfidence * 100)}%)
                  </span>
                : <span className="snippet-key-inline snippet-key-empty">· no key detected</span>}
              {snippet.savedPath ? <span className="snippet-saved"> · saved</span> : null}
            </div>
            <div className="snippet-actions">
              <button
                type="button"
                onClick={handleDetectKey}
                disabled={detecting}
                title={hasKey ? "Re-detect the musical key from the audio" : "Detect the musical key from the audio"}
              >
                {detecting ? "Detecting…" : hasKey ? "↻ Re-detect key" : "Detect key"}
              </button>
              <button type="button" onClick={handleSave} title="Save snippet to a WAV file">Save</button>
              <button type="button" onClick={handleReveal} disabled={!snippet.savedPath} title="Reveal saved file in Explorer">Reveal</button>
              <button type="button" className="danger" onClick={handleDelete} title="Delete snippet">Delete</button>
            </div>
          </footer>
        </div>
      ) : null}
    </article>
  );
}
