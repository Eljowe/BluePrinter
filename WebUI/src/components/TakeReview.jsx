import { FRONTEND_EVENTS, emit } from "../bridge";
import { IconPlay, IconSave, IconStop, IconTrash } from "./icons";
import { Waveform } from "./Waveform";
import { formatTime } from "../utils";

// Review panel for the last recorded take. Recordings are no longer
// saved automatically — after stopping, the take stays pending here
// until it is replayed and either saved to the library or discarded.
export function TakeReview({ transport }) {
  const pending = Boolean(transport?.takePending);
  if (!pending) return null;

  const playing = Boolean(transport?.takePlaying);
  const sampleRate = Number(transport?.recordingSampleRate ?? 0);
  const length = Number(transport?.takeLength ?? 0);
  const seconds = sampleRate > 0 ? length / sampleRate : 0;
  const progress = length > 0
    ? Math.min(100, Math.max(0, (Number(transport?.takePosition ?? 0) / length) * 100))
    : 0;

  return (
    <section className={`take-review ${playing ? "is-playing" : ""}`}>
      <div className="take-review-header">
        <div>
          <h2>Review the take</h2>
          <p>Play it back, then save it to the library or discard it. A new recording replaces it.</p>
        </div>
        <div className="take-review-state" aria-live="polite">
          <span className="take-review-state-dot" />
          {playing ? "Playing" : `${formatTime(seconds)} take`}
        </div>
      </div>

      <div className="take-review-timeline" aria-label={`${formatTime(seconds)} pending take`}>
        <Waveform peaks={transport.takePeaks ?? []} width={360} height={72} />
        <div className="take-review-playhead" style={{ left: `${progress}%` }} />
        <div className="take-review-timeline-caption">
          <span>{formatTime(seconds)}</span>
          <span>unsaved</span>
        </div>
      </div>

      <div className="take-review-actions">
        <button
          type="button"
          className="btn btn-primary btn-sm"
          onClick={() => emit(FRONTEND_EVENTS.setTakePlayback, { enabled: !playing })}
        >
          {playing ? <IconStop size={13} /> : <IconPlay size={13} />} {playing ? "Stop take" : "Play take"}
        </button>
        <button
          type="button"
          className="btn btn-ghost btn-sm"
          disabled={playing}
          onClick={() => emit(FRONTEND_EVENTS.saveTake)}
          title="Save the take to the library (and to the library folder if one is set)"
        >
          <IconSave size={13} /> Save to library
        </button>
        <button
          type="button"
          className="btn btn-ghost btn-sm"
          disabled={playing}
          onClick={() => emit(FRONTEND_EVENTS.discardTake)}
          title="Delete the take without saving"
        >
          <IconTrash size={13} /> Discard
        </button>
      </div>
    </section>
  );
}
