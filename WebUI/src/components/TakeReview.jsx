import { useRef } from "react";
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

  const recording = Boolean(transport?.recording) || Boolean(transport?.preRollActive)
    || Boolean(transport?.looperRecording) || Boolean(transport?.looperPreRoll);
  const playing = Boolean(transport?.takePlaying);
  const overdub = Boolean(transport?.takeOverdub);
  const sampleRate = Number(transport?.recordingSampleRate ?? 0);
  const loopCapture = Boolean(transport?.looperRecording) || Boolean(transport?.looperPreRoll);

  const selectedId = Number(transport?.selectedTakeId ?? -1);
  const selected = takes.find((t) => Number(t.id) === selectedId) ?? takes[takes.length - 1];
  const length = Number(transport?.takeLength ?? selected?.length ?? 0);

  const stateLabel = recording
    ? (loopCapture ? "Loop capture" : (overdub ? "Overdub" : "Recording"))
    : (playing ? "Playing" : `${takes.length} ${takes.length === 1 ? "take" : "takes"}`);

  const select = (id) => { if (!recording) emit(FRONTEND_EVENTS.selectTake, { id }); };
  const remove = (id) => { if (!recording) emit(FRONTEND_EVENTS.discardTake, { id }); };

  return (
    <section className={`take-review ${playing ? "is-playing" : ""} ${recording ? "is-recording" : ""}`}>
      <div className="take-review-header">
        <div>
          <h2>Review takes</h2>
          <p>
            {recording
              ? (loopCapture ? "Stop the loop capture to review or edit takes." : overdub
                ? "Layering over the selected take — stop to mix the new pass in."
                : "Recording a new take — stop to add it to the stack.")
              : "Click to audition, drag the trim handles to keep a region, then save. Dub loops the kept region; the full source stays available. Melody analyses the full take."}
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
              tabIndex={recording ? -1 : 0}
              aria-disabled={recording}
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

      <TakeWaveform key={selected?.id} transport={transport} length={length} sampleRate={sampleRate} disabled={recording} />

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
          title="Save only the kept region to the library (and the library folder if one is set)"
        >
          <IconSave size={13} /> Save selected
        </button>
        <button
          type="button"
          className="btn btn-ghost btn-sm"
          disabled={!transport?.takeUndoAvailable || playing || recording || transport?.melodyPlaying
            || transport?.looperPlaying || Number(transport?.playingSnippetId ?? -1) >= 0}
          onClick={() => emit(FRONTEND_EVENTS.takeUndo)}
          title="Undo the last completed overdub; keep the current trim (Ctrl/Cmd+Z)"
        >
          Undo overdub
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

function TakeWaveform({ transport, length, sampleRate, disabled }) {
  const trackRef = useRef(null);
  const drag = useRef(null);
  const trimStart = Math.max(0, Math.min(length - 1, Number(transport.takeTrimStart ?? 0)));
  const trimEnd = length > 0 ? Math.max(trimStart + 1, Math.min(length, Number(transport.takeTrimEnd ?? length))) : 0;
  const position = Math.max(0, Math.min(length, Number(transport.takePosition ?? trimStart)));
  const unavailable = disabled || length <= 0 || sampleRate <= 0;
  const percent = (sample) => length > 0 ? sample / length * 100 : 0;
  const time = (sample) => `${(sampleRate > 0 ? sample / sampleRate : 0).toFixed(3)} s`;

  const seek = (sample) => {
    if (unavailable) return;
    const target = Math.max(trimStart, Math.min(trimEnd - 1, Math.round(sample)));
    if (transport.takePlaying) emit(FRONTEND_EVENTS.setTakePlaybackPosition, { position: target });
    else emit(FRONTEND_EVENTS.setTakePlayback, { enabled: true, startSample: target });
  };

  const changeTrim = (edge, sample) => {
    if (unavailable) return;
    const startSample = edge === "start" ? Math.max(0, Math.min(trimEnd - 1, Math.round(sample))) : trimStart;
    const endSample = edge === "end" ? Math.max(trimStart + 1, Math.min(length, Math.round(sample))) : trimEnd;
    emit(FRONTEND_EVENTS.setTakeTrim, { startSample, endSample });
  };

  const pointerSample = (e) => {
    const rect = trackRef.current.getBoundingClientRect();
    return Math.round(Math.max(0, Math.min(1, (e.clientX - rect.left) / Math.max(1, rect.width))) * length);
  };

  const pointerDown = (e, edge = "seek") => {
    if (unavailable || e.button !== 0) return;
    e.preventDefault();
    e.currentTarget.focus();
    e.currentTarget.setPointerCapture(e.pointerId);
    drag.current = edge;
    if (edge === "seek") seek(pointerSample(e));
    else changeTrim(edge, pointerSample(e));
  };

  const pointerMove = (e) => {
    if (!drag.current || unavailable) return;
    if (drag.current === "seek") seek(pointerSample(e));
    else changeTrim(drag.current, pointerSample(e));
  };

  const pointerUp = (e) => {
    drag.current = null;
    if (e.currentTarget.hasPointerCapture(e.pointerId)) e.currentTarget.releasePointerCapture(e.pointerId);
  };

  const keyDown = (e, edge = "seek") => {
    if (unavailable || e.ctrlKey || e.metaKey || e.altKey) return;
    const min = edge === "seek" ? trimStart : (edge === "end" ? trimStart + 1 : 0);
    const max = edge === "seek" ? trimEnd - 1 : (edge === "start" ? trimEnd - 1 : length);
    const current = edge === "seek" ? position : (edge === "start" ? trimStart : trimEnd);
    const step = Math.max(1, Math.round(sampleRate * 0.1));
    let next;
    if (e.key === "ArrowRight" || e.key === "ArrowUp") next = current + step;
    else if (e.key === "ArrowLeft" || e.key === "ArrowDown") next = current - step;
    else if (e.key === "PageUp") next = current + step * 10;
    else if (e.key === "PageDown") next = current - step * 10;
    else if (e.key === "Home") next = min;
    else if (e.key === "End") next = max;
    else return;
    e.preventDefault();
    next = Math.max(min, Math.min(max, next));
    if (edge === "seek") seek(next);
    else changeTrim(edge, next);
  };

  return (
    <div className="take-review-trim">
      <div className={`take-review-timeline ${unavailable ? "is-disabled" : ""}`}>
        <div className="take-review-track" ref={trackRef}>
          <Waveform peaks={transport.takePeaks ?? []} width={360} height={72} />
          <div className="take-review-discarded" style={{ left: 0, width: `${percent(trimStart)}%` }} />
          <div className="take-review-discarded" style={{ left: `${percent(trimEnd)}%`, right: 0 }} />
          <div className="take-review-playhead" style={{ left: `${percent(position)}%` }} />
          <div
            className="take-review-seek"
            role="slider" tabIndex={unavailable ? -1 : 0}
            aria-label="Selected take playback position"
            aria-disabled={unavailable}
            aria-valuemin={0} aria-valuemax={length} aria-valuenow={position}
            aria-valuetext={`${time(position)} from source start`}
            title="Click to play; drag to scrub. Arrows: 0.1 s; Page Up/Down: 1 s; Home/End: kept region limits."
            onPointerDown={pointerDown} onPointerMove={pointerMove}
            onPointerUp={pointerUp} onPointerCancel={pointerUp}
            onKeyDown={keyDown}
          />
          {["start", "end"].map((edge) => (
            <div
              key={edge} className={`take-review-trim-handle is-${edge}`}
              style={{ left: `${percent(edge === "start" ? trimStart : trimEnd)}%` }}
              role="slider" tabIndex={unavailable ? -1 : 0}
              aria-label={`Take trim ${edge}`} aria-disabled={unavailable}
              aria-valuemin={edge === "start" ? 0 : trimStart + 1}
              aria-valuemax={edge === "start" ? trimEnd - 1 : length}
              aria-valuenow={edge === "start" ? trimStart : trimEnd}
              aria-valuetext={`${time(edge === "start" ? trimStart : trimEnd)} from source start${edge === "end" ? ", exclusive" : ""}`}
              title={`Trim ${edge}. Drag or use arrows (0.1 s), Page Up/Down (1 s), Home/End.`}
              onPointerDown={(e) => pointerDown(e, edge)} onPointerMove={pointerMove}
              onPointerUp={pointerUp} onPointerCancel={pointerUp}
              onKeyDown={(e) => keyDown(e, edge)}
            />
          ))}
        </div>
        <div className="take-review-timeline-caption">
          <span>{formatTime(sampleRate > 0 ? length / sampleRate : 0)} source</span>
          <span>unsaved</span>
        </div>
      </div>
      <div className="take-review-trim-info">
        <span><strong>{time(trimEnd - trimStart)} kept</strong> · {time(trimStart)} to {time(trimEnd)} from source start</span>
        <button
          type="button" className="btn btn-ghost btn-sm"
          disabled={unavailable || (trimStart === 0 && trimEnd === length)}
          onClick={() => emit(FRONTEND_EVENTS.setTakeTrim, { startSample: 0, endSample: length })}
          title="Restore the full source for audition, overdub and save"
        >Reset trim</button>
      </div>
    </div>
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

  const auditionFrom = (startSample) => {
    if (!recording) emit(FRONTEND_EVENTS.setMelodyPlayback, { enabled: true, startSample });
  };

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
