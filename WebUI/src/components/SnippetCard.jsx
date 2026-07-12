import { useEffect, useRef, useState } from "react";
import { Waveform } from "./Waveform";
import { formatTime } from "../utils";
import { FRONTEND_EVENTS, emit } from "../bridge";

export function SnippetCard({ snippet, isPlaying, playPositionSeconds }) {
  const [name, setName] = useState(snippet.name ?? "");
  const [comments, setComments] = useState(snippet.comments ?? "");

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
    }
  }, [snippet.id, snippet.name, snippet.comments]);

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

  const duration = Number(snippet.durationSeconds) || 0;
  const showPosition = isPlaying ? Math.min(duration, playPositionSeconds) : 0;

  return (
    <article className={`snippet ${isPlaying ? "is-playing" : ""}`}>
      <header className="snippet-header">
        <div className="snippet-title">
          <span className="snippet-id">#{snippet.id}</span>
          <span className="snippet-duration">{formatTime(showPosition)} / {formatTime(duration)}</span>
        </div>
        <div className="snippet-header-actions">
          <button
            type="button"
            className={`play-button ${isPlaying ? "is-playing" : ""}`}
            onClick={handlePlay}
            title={isPlaying ? "Stop playback" : "Play snippet"}
          >
            {isPlaying ? "■" : "▶"}
          </button>
        </div>
      </header>

      <div className="snippet-waveform">
        <Waveform peaks={snippet.peaks ?? []} width={520} height={56} />
        {isPlaying ? (
          <div
            className="snippet-playhead"
            style={{ left: `${duration > 0 ? (playPositionSeconds / duration) * 100 : 0}%` }}
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
          {Number(snippet.numChannels) || 0}ch · {Math.round(Number(snippet.sampleRate) || 0)} Hz
          {snippet.savedPath ? <span className="snippet-saved"> · saved</span> : null}
        </div>
        <div className="snippet-actions">
          <button type="button" onClick={handleSave} title="Save snippet to a WAV file">Save</button>
          <button type="button" onClick={handleReveal} disabled={!snippet.savedPath} title="Reveal saved file in Explorer">Reveal</button>
          <button type="button" className="danger" onClick={handleDelete} title="Delete snippet">Delete</button>
        </div>
      </footer>
    </article>
  );
}
