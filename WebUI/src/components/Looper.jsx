import { useCallback, useEffect, useRef, useState } from "react";
import { FRONTEND_EVENTS, emit } from "../bridge";
import { IconPlay, IconSave, IconStop, IconTrash } from "./icons";
import { LevelMeter } from "./LevelMeter";
import { Waveform } from "./Waveform";

// Shared with the transport: count-in field with a "beats" suffix.
function NumberInput({ value, min, max, step, className, onChange, suffix, title }) {
  const [text, setText] = useState(String(value));
  const committedRef = useRef(value);

  useEffect(() => {
    if (value !== committedRef.current) {
      setText(String(value));
      committedRef.current = value;
    }
  }, [value]);

  const flush = useCallback((raw) => {
    const parsed = parseInt(raw, 10);
    if (!Number.isNaN(parsed)) {
      const clamped = Math.max(min, Math.min(max, parsed));
      committedRef.current = clamped;
      onChange(clamped);
      setText(String(clamped));
    } else {
      setText(String(committedRef.current));
    }
  }, [min, max, onChange]);

  return (
    <div className={className}>
      <input
        type="number"
        min={min}
        max={max}
        step={step}
        value={text}
        onChange={(e) => setText(e.target.value)}
        onBlur={(e) => flush(e.target.value)}
        onKeyDown={(e) => { if (e.key === "Enter") e.currentTarget.blur(); }}
        title={title}
      />
      {suffix ? <span className="count-in-suffix">{suffix}</span> : null}
    </div>
  );
}

function Stepper({ label, value, min, max, onChange, title }) {
  const step = (delta) => {
    const next = Math.max(min, Math.min(max, value + delta));
    if (next !== value) onChange(next);
  };
  return (
    <div className="looper-stepper" title={title}>
      <span className="looper-stepper-label">{label}</span>
      <div className="looper-stepper-control">
        <button type="button" className="looper-stepper-btn" disabled={value <= min} onClick={() => step(-1)} aria-label={`${label}: decrease`}>−</button>
        <span className="looper-stepper-value">{value}</span>
        <button type="button" className="looper-stepper-btn" disabled={value >= max} onClick={() => step(1)} aria-label={`${label}: increase`}>+</button>
      </div>
    </div>
  );
}

// Formats a beat count for captions: whole bars when possible, beats
// otherwise (crop is beat-granular).
function formatBeats(beats) {
  if (beats <= 0) return "0 beats";
  if (beats % 4 === 0) {
    const bars = beats / 4;
    return `${bars} bar${bars === 1 ? "" : "s"}`;
  }
  return `${beats} beats`;
}

export function Looper({ transport, onOverdubChange }) {
  const recording = Boolean(transport?.looperRecording);
  const preRoll = Boolean(transport?.looperPreRoll);
  const isRecording = recording || preRoll;
  const playing = Boolean(transport?.looperPlaying);
  const looping = transport?.looperLooping !== false;
  const overdub = Boolean(transport?.looperOverdub);

  const loopLength = Number(transport?.audioLoopLength ?? 0);
  const hasLoop = loopLength > 0;

  // A capture layers over the loop when overdub is on, looping is on and
  // a loop exists — matches the backend's condition at capture start.
  const isOverdubbing = isRecording && overdub && hasLoop;
  const countInBeats = Number(transport?.looperCountInBeats ?? 0);
  const cropStartBeats = Number(transport?.looperCropStartBeats ?? 0);
  const cropEndBeats = Number(transport?.looperCropEndBeats ?? 0);
  const loopStart = Number(transport?.audioLoopStart ?? 0);

  // Fresh-capture progress: the timeline fills as the capture grows
  // toward the record buffer's capacity.
  const captureMax = Number(transport?.maxRecordSamples ?? 0);
  const capturePct = captureMax > 0 && isRecording && !isOverdubbing
    ? Math.min(100, Math.max(0, (loopLength / captureMax) * 100))
    : 0;

  // The captured loop is trimmed to whole bars, so the beat count can be
  // derived from the current BPM/sample rate.
  const beatSamples = Number(transport?.bpm ?? 120) > 0 && Number(transport?.recordingSampleRate ?? 0) > 0
    ? (60.0 / Number(transport.bpm)) * Number(transport.recordingSampleRate)
    : 0;
  // The cropped window + the cropped-off beats are the FULL loop, which
  // is what the crop beats are measured against in the backend — so the
  // stepper ranges and the crop overlays stay anchored and moving a crop
  // handle back toward 0 restores the region it cut off instead of
  // shrinking the window further.
  const totalBeats = beatSamples > 0 && loopLength > 0
    ? Math.max(1, Math.round(loopLength / beatSamples) + cropStartBeats + cropEndBeats)
    : 0;
  const croppedBeats = totalBeats > 0 ? Math.max(0, totalBeats - cropStartBeats - cropEndBeats) : 0;

  // The waveform shows the FULL loop, so the playhead maps the absolute
  // window position onto the full sample range.
  const fullSamples = beatSamples > 0 && totalBeats > 0 ? totalBeats * beatSamples : loopLength;
  const loopProgress = fullSamples > 0
    ? Math.min(100, Math.max(0, ((loopStart + Number(transport.audioLoopPosition ?? 0)) / fullSamples) * 100))
    : 0;
  const cropStartPct = totalBeats > 0 ? (cropStartBeats / totalBeats) * 100 : 0;
  const cropEndPct = totalBeats > 0 ? (cropEndBeats / totalBeats) * 100 : 0;

  // Count-in countdown, identical to the take transport: the pre-roll
  // advances transportPosition in processBlock, so the beat boundary is
  // derived from the shared BPM/sample rate and the remaining beats
  // count down from the configured count-in length.
  let countdown = null;
  if (preRoll && Number(transport?.bpm ?? 0) > 0 && Number(transport?.recordingSampleRate ?? 0) > 0) {
    const samplesPerBeat = (60.0 / Number(transport.bpm)) * Number(transport.recordingSampleRate);
    const currentBeat = Math.floor((Number(transport.transportPosition ?? 0)) / samplesPerBeat);
    countdown = Math.max(1, countInBeats - currentBeat);
  }

  const setRecording = (enabled) => emit(FRONTEND_EVENTS.setLooperRecording, { enabled });
  const setPlaying = (enabled) => emit(FRONTEND_EVENTS.setLooperPlaying, { enabled });
  const emitCrop = (startBeats, endBeats) => emit(FRONTEND_EVENTS.setLoopCrop, { startBeats, endBeats });

  return (
    <section className={`looper ${isRecording ? "is-recording" : ""} ${playing ? "is-playing" : ""} ${preRoll ? "is-counting-in" : ""}`}>
      <div className="looper-header">
        <div>
          <h2>Capture a loop. Play over it.</h2>
          <p>Records whatever the chains make — synth, guitar, FX — so the loop sounds exactly like what you heard.</p>
        </div>
        <div className="looper-state" aria-live="polite">
          <span className="looper-state-dot" />
          {preRoll ? "Count-in" : isOverdubbing ? "Overdub" : recording ? "Recording" : playing ? "Playing" : hasLoop ? `${formatBeats(croppedBeats)} loop ready` : "Empty"}
        </div>
      </div>

      <div className={`looper-timeline ${preRoll ? "is-counting-in" : ""}`} aria-label={`${formatBeats(croppedBeats)} loop`}>
        <div className="looper-grid-lines"><i /><i /><i /><i /><i /><i /><i /><i /></div>
        {isRecording && !isOverdubbing && capturePct > 0 ? (
          <div className="looper-capture-progress" style={{ width: `${capturePct}%` }} />
        ) : null}
        {hasLoop ? (
          <div className="looper-waveform">
            <Waveform peaks={transport.audioLoopPeaks ?? []} width={360} height={88} />
          </div>
        ) : null}
        {preRoll && countdown != null ? (
          <div className="looper-countdown" key={countdown} aria-hidden="true">
            {countdown}
          </div>
        ) : null}
        {hasLoop ? <div className="looper-crop-left" style={{ width: `${cropStartPct}%` }} /> : null}
        {hasLoop ? <div className="looper-crop-right" style={{ width: `${cropEndPct}%` }} /> : null}
        {hasLoop ? <div className="looper-playhead" style={{ left: `${loopProgress}%` }} /> : null}
        <div className="looper-timeline-caption">
          <span>{hasLoop ? formatBeats(croppedBeats) : "No loop captured yet"}</span>
          <span>{looping ? "LOOP" : "ONE SHOT"}</span>
        </div>
      </div>

      <div className="looper-controls">
        <div className="looper-primary-controls">
          <button
            type="button"
            className={`looper-record-button ${isRecording ? "is-active" : ""}`}
            onClick={() => setRecording(!isRecording)}
            title={isRecording ? "Stop loop recording" : "Record loop"}
            aria-pressed={isRecording}
            aria-label={isRecording ? "Stop loop recording" : "Record loop"}
          >
            <span className="looper-record-dot" aria-hidden="true" />
          </button>
          <div className="looper-meter" aria-hidden={!isRecording}>
            <LevelMeter level={transport?.inputLevel ?? 0} peak={transport?.inputPeak ?? 0} />
          </div>
          <button type="button" className="btn btn-primary btn-sm" disabled={!hasLoop} onClick={() => setPlaying(!playing)}>
            {playing ? <IconStop size={13} /> : <IconPlay size={13} />} {playing ? "Stop loop" : "Play loop"}
          </button>
          <button
            type="button"
            className="btn btn-ghost btn-sm"
            disabled={!hasLoop && !isRecording}
            onClick={() => emit(FRONTEND_EVENTS.clearLoop)}
            title="Delete the loop without saving"
          >
            <IconTrash size={13} /> Discard
          </button>
          <button type="button" className="btn btn-ghost btn-sm" disabled={!hasLoop} onClick={() => emit(FRONTEND_EVENTS.saveLoop)}>
            <IconSave size={13} /> Save to library
          </button>
        </div>

        <div className="looper-settings">
          <label className="count-in-control">
            <span className="count-in-label">Count-in</span>
            <NumberInput
              className="count-in-field"
              min={0}
              max={8}
              step={1}
              value={countInBeats}
              onChange={(beats) => emit(FRONTEND_EVENTS.setLooperCountIn, { beats })}
              suffix="beats"
              title="Beats of click before the loop capture starts (0 = off)"
            />
          </label>

          <label className={`looper-loop-switch ${!looping ? "is-disabled" : ""}`}>
            <input type="checkbox" checked={looping} onChange={(e) => emit(FRONTEND_EVENTS.setLooperLooping, { enabled: e.target.checked })} />
            <span className="looper-switch" />
            <span>{looping ? "Loop" : "One shot"}</span>
          </label>

          <label
            className={`looper-loop-switch ${!looping ? "is-disabled" : ""}`}
            title={looping
              ? (overdub ? "Overdub on — record layers over the loop" : "Overdub off — record replaces the loop. Turn on to layer.")
              : "Overdub needs loop mode"}
          >
            <input
              type="checkbox"
              checked={overdub}
              disabled={!looping}
              onChange={(e) => onOverdubChange(e.target.checked)}
            />
            <span className="looper-switch" />
            <span>Overdub</span>
          </label>

          <Stepper
            label="Crop start"
            value={cropStartBeats}
            min={0}
            max={!recording && totalBeats > 0 ? Math.max(0, totalBeats - 1 - cropEndBeats) : 0}
            onChange={(beats) => emitCrop(beats, cropEndBeats)}
            title="Beats to trim off the start of the loop (4 beats per bar)"
          />

          <Stepper
            label="Crop end"
            value={cropEndBeats}
            min={0}
            max={!recording && totalBeats > 0 ? Math.max(0, totalBeats - 1 - cropStartBeats) : 0}
            onChange={(beats) => emitCrop(cropStartBeats, beats)}
            title="Beats to trim off the end of the loop (4 beats per bar)"
          />
        </div>
      </div>
    </section>
  );
}
