import { FRONTEND_EVENTS, emit } from "../bridge";
import { IconPlay, IconSave, IconStop, IconTrash } from "./icons";
import { Waveform } from "./Waveform";
import { PianoRoll, midiToNoteName } from "./PianoRoll";
import { formatTime } from "../utils";

// Review panel for the take stack (0037). Every stopped take is retained
// (bounded); the chip row selects one to audition, save or delete. The
// waveform shows the selected take and Dub layers onto it.
export function TakeReview({ transport }) {
  const takes = Array.isArray(transport?.takes) ? transport.takes : [];
  if (takes.length === 0) return null;

  const recording = Boolean(transport?.recording) || Boolean(transport?.preRollActive);
  const playing = Boolean(transport?.takePlaying);
  const overdub = Boolean(transport?.takeOverdub);
  const sampleRate = Number(transport?.recordingSampleRate ?? 0);

  const selectedId = Number(transport?.selectedTakeId ?? -1);
  const selected = takes.find((t) => Number(t.id) === selectedId) ?? takes[takes.length - 1];
  const length = Number(transport?.takeLength ?? selected?.length ?? 0);
  const seconds = sampleRate > 0 ? length / sampleRate : 0;
  const progress = length > 0
    ? Math.min(100, Math.max(0, (Number(transport?.takePosition ?? 0) / length) * 100))
    : 0;

  const stateLabel = recording
    ? (overdub ? "Overdub" : "Recording")
    : (playing ? "Playing" : `${takes.length} ${takes.length === 1 ? "take" : "takes"}`);

  const select = (id) => emit(FRONTEND_EVENTS.selectTake, { id });
  const remove = (id) => emit(FRONTEND_EVENTS.discardTake, { id });

  return (
    <section className={`take-review ${playing ? "is-playing" : ""} ${recording ? "is-recording" : ""}`}>
      <div className="take-review-header">
        <div>
          <h2>Review takes</h2>
          <p>
            {recording
              ? (overdub
                ? "Layering over the selected take — stop to mix the new pass in."
                : "Recording a new take — stop to add it to the stack.")
              : "Pick a take to play, then save it to the library or delete it. Turn on Dub to layer another pass onto the selected take."}
          </p>
        </div>
        <div className="take-review-state" aria-live="polite">
          <span className="take-review-state-dot" />
          {stateLabel}
        </div>
      </div>

      <div className="take-review-takes" role="listbox" aria-label="Recorded takes">
        {takes.map((t, index) => {
          const isSelected = Number(t.id) === Number(selected?.id);
          const duration = sampleRate > 0 ? Number(t.length) / sampleRate : 0;
          return (
            <div
              key={t.id}
              className={`take-chip ${isSelected ? "is-selected" : ""}`}
              role="option"
              aria-selected={isSelected}
              tabIndex={0}
              onClick={() => select(t.id)}
              onKeyDown={(e) => {
                if (e.key === "Enter" || e.key === " ") {
                  e.preventDefault();
                  select(t.id);
                }
              }}
            >
              <span className="take-chip-label">Take {index + 1}</span>
              <span className="take-chip-time">{formatTime(duration)}</span>
              <button
                type="button"
                className="take-chip-delete"
                disabled={recording}
                aria-label={`Delete take ${index + 1}`}
                onClick={(e) => {
                  e.stopPropagation();
                  remove(t.id);
                }}
              >
                <IconTrash size={12} />
              </button>
            </div>
          );
        })}
      </div>

      <div className="take-review-timeline" aria-label={`${formatTime(seconds)} selected take`}>
        <Waveform peaks={transport.takePeaks ?? []} width={360} height={72} />
        <div className="take-review-playhead" style={{ left: `${progress}%` }} />
        <div className="take-review-timeline-caption">
          <span>{formatTime(seconds)}</span>
          <span>unsaved</span>
        </div>
      </div>

      <MelodyPanel
        transport={transport}
        selectedTakeId={selected?.id}
        totalSamples={length}
        sampleRate={sampleRate}
        recording={recording}
      />

      <label
        className={`take-review-dub ${recording ? "is-disabled" : ""}`}
        title={recording
          ? "Stop the capture to change Dub"
          : (overdub
            ? "Dub on — the next record layers over the selected take"
            : "Dub off — the next record adds a new take. Turn on to layer.")}
      >
        <input
          type="checkbox"
          checked={overdub}
          disabled={recording}
          onChange={(e) => emit(FRONTEND_EVENTS.setTakeOverdub, { enabled: e.target.checked })}
        />
        <span className="take-review-dub-switch" />
        <span>Dub</span>
      </label>

      <div className="take-review-actions">
        <button
          type="button"
          className="btn btn-primary btn-sm"
          disabled={recording}
          onClick={() => emit(FRONTEND_EVENTS.setTakePlayback, { enabled: !playing })}
        >
          {playing ? <IconStop size={13} /> : <IconPlay size={13} />} {playing ? "Stop take" : "Play take"}
        </button>
        <button
          type="button"
          className="btn btn-ghost btn-sm"
          disabled={playing || recording}
          onClick={() => emit(FRONTEND_EVENTS.saveTake, { id: selected?.id })}
          title="Save the selected take to the library (and the library folder if one is set)"
        >
          <IconSave size={13} /> Save selected
        </button>
        {transport?.stemsAvailable && transport?.stemSource === "take" ? (
          <button
            type="button"
            className="btn btn-ghost btn-sm"
            disabled={playing || recording}
            onClick={() => emit(FRONTEND_EVENTS.exportStems, { source: "take" })}
            title="Export one file per chain (plus the dry input) — the stems sum back to the captured take"
          >
            Export stems
          </button>
        ) : null}
        <button
          type="button"
          className="btn btn-ghost btn-sm"
          disabled={playing || recording}
          onClick={() => emit(FRONTEND_EVENTS.discardTake, { id: selected?.id })}
          title="Delete the selected take without saving"
        >
          <IconTrash size={13} /> Delete selected
        </button>
        <button
          type="button"
          className="btn btn-ghost btn-sm"
          disabled={playing || recording}
          onClick={() => emit(FRONTEND_EVENTS.discardAllTakes)}
          title="Delete every recorded take"
        >
          <IconTrash size={13} /> Discard all
        </button>
      </div>
    </section>
  );
}

// Melody extraction (0054) for the selected take: analyse the pitches, show
// them as a piano-roll with the detected key, and audition them with the
// built-in synth.
function MelodyPanel({ transport, selectedTakeId, totalSamples, sampleRate, recording }) {
  const notes = Array.isArray(transport?.melodyNotes) ? transport.melodyNotes : [];
  const hasMelody = notes.length > 0;
  const analysing = Boolean(transport?.melodyAnalysing);
  const analysed = Boolean(transport?.melodyAnalysed);
  const playing = Boolean(transport?.melodyPlaying);

  const bpm = Number(transport?.bpm ?? 0);
  const beatsPerBar = Number(transport?.timeSignatureNumerator ?? 4) || 4;

  const auditionFrom = (startSample) =>
    emit(FRONTEND_EVENTS.setMelodyPlayback, { enabled: true, startSample });

  const copyMelody = () => {
    const sr = Number(sampleRate) > 0 ? Number(sampleRate) : 0;
    const lines = notes.map((n) => {
      const name = midiToNoteName(n.midi);
      if (sr <= 0) return name;
      const start = (Number(n.startSample) || 0) / sr;
      const length = (Number(n.lengthSamples) || 0) / sr;
      return `${name}\t${start.toFixed(3)}s\t${length.toFixed(3)}s`;
    });
    const header = transport?.melodyKey ? `Key: ${transport.melodyKey}` : "";
    const text = [header, ...lines].filter(Boolean).join("\n");
    try {
      navigator.clipboard?.writeText(text);
    } catch {
      /* clipboard unavailable — ignore */
    }
  };

  let status = "";
  let statusKind = "is-hint";
  if (analysing) {
    status = "Analysing melody…";
    statusKind = "is-analysing";
  } else if (hasMelody) {
    status = `${notes.length} note${notes.length === 1 ? "" : "s"} found`;
    statusKind = "is-ok";
  } else if (analysed) {
    status = "No melody found — try a longer, sustained hum or a single sung line.";
    statusKind = "is-empty";
  } else if (recording) {
    status = "Stop the capture to analyse the melody.";
  } else {
    status = "Analyse the take to see and hear its melody.";
  }

  const buttonLabel = analysing
    ? "Analysing…"
    : (hasMelody || analysed ? "Re-analyse" : "Analyse melody");

  return (
    <div className={`take-review-melody ${playing ? "is-playing" : ""}`}>
      <div className="take-review-melody-head">
        <span className="take-review-melody-title">Melody</span>
        {transport?.melodyKey ? (
          <span className="take-review-melody-key" title="Detected key">
            Key {transport.melodyKey}
          </span>
        ) : null}
        {analysing ? <span className="melody-spinner" aria-hidden="true" /> : null}
        {hasMelody && !analysing ? (
          <span className="take-review-melody-hint">scroll zoom · shift-scroll pan · click a note to hear it</span>
        ) : null}
      </div>

      <PianoRoll
        notes={notes}
        totalSamples={totalSamples}
        sampleRate={sampleRate}
        position={Number(transport?.melodyPosition ?? 0)}
        playing={playing}
        keySignature={transport?.melodyKey ?? ""}
        bpm={bpm}
        beatsPerBar={beatsPerBar}
        onAudition={auditionFrom}
      />

      <p className={`take-review-melody-status ${statusKind}`} role="status" aria-live="polite">
        {status}
      </p>

      <div className="take-review-melody-actions">
        <button
          type="button"
          className="btn btn-ghost btn-sm"
          disabled={recording || analysing}
          onClick={() => emit(FRONTEND_EVENTS.analyzeTakeMelody, { id: selectedTakeId })}
          title="Extract the melody (monophonic pitches) from this take"
        >
          {buttonLabel}
        </button>
        <button
          type="button"
          className="btn btn-ghost btn-sm"
          disabled={recording || !hasMelody}
          onClick={() => emit(FRONTEND_EVENTS.setMelodyPlayback, { enabled: !playing })}
          title="Play the extracted melody with the built-in synth (monitor only)"
        >
          {playing ? <IconStop size={13} /> : <IconPlay size={13} />} {playing ? "Stop melody" : "Play melody"}
        </button>
        <button
          type="button"
          className="btn btn-ghost btn-sm"
          disabled={!hasMelody}
          onClick={copyMelody}
          title="Copy the detected key and note list (name, start, length) to the clipboard"
        >
          Copy notes
        </button>
      </div>
    </div>
  );
}
