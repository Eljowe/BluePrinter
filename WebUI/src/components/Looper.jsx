import { FRONTEND_EVENTS, emit } from "../bridge";
import { IconMetronome, IconPlay, IconSave, IconStop, IconTrash } from "./icons";

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

export function Looper({ transport }) {
  const recording = Boolean(transport?.looperRecording);
  const preRoll = Boolean(transport?.looperPreRoll);
  const isRecording = recording || preRoll;
  const playing = Boolean(transport?.looperPlaying);
  const looping = transport?.looperLooping !== false;
  const clickEnabled = transport?.looperClickEnabled !== false;
  const countInBeats = Number(transport?.looperCountInBeats ?? 0);
  const cropStartBars = Number(transport?.looperCropStartBars ?? 0);
  const cropEndBars = Number(transport?.looperCropEndBars ?? 0);

  const loopLength = Number(transport?.audioLoopLength ?? 0);
  const hasLoop = loopLength > 0;

  // The captured loop is trimmed to whole bars, so the bar count can be
  // derived from the current BPM/sample rate.
  const barSamples = Number(transport?.bpm ?? 120) > 0 && Number(transport?.recordingSampleRate ?? 0) > 0
    ? (60.0 / Number(transport.bpm)) * Number(transport.recordingSampleRate) * 4.0
    : 0;
  const totalBars = barSamples > 0 && loopLength > 0
    ? Math.max(1, Math.round(loopLength / barSamples))
    : 0;
  const croppedBars = totalBars > 0 ? Math.max(1, totalBars - cropStartBars - cropEndBars) : 0;

  const loopProgress = loopLength > 0
    ? Math.min(100, Math.max(0, (Number(transport.audioLoopPosition ?? 0) / loopLength) * 100))
    : 0;
  const cropStartPct = totalBars > 0 ? (cropStartBars / totalBars) * 100 : 0;
  const cropEndPct = totalBars > 0 ? (cropEndBars / totalBars) * 100 : 0;

  const setRecording = (enabled) => emit(FRONTEND_EVENTS.setLooperRecording, { enabled });
  const setPlaying = (enabled) => emit(FRONTEND_EVENTS.setLooperPlaying, { enabled });
  const emitCrop = (startBars, endBars) => emit(FRONTEND_EVENTS.setLoopCrop, { startBars, endBars });

  return (
    <section className={`looper ${isRecording ? "is-recording" : ""} ${playing ? "is-playing" : ""}`}>
      <div className="looper-header">
        <div>
          <div className="eyebrow">Looper</div>
          <h2>Capture a loop. Play over it.</h2>
          <p>Records whatever the chains make — synth, guitar, FX — so the loop sounds exactly like what you heard.</p>
        </div>
        <div className="looper-state" aria-live="polite">
          <span className="looper-state-dot" />
          {preRoll ? `Count-in ${countInBeats}` : recording ? "Recording" : playing ? "Loop playing" : hasLoop ? `${croppedBars} bar loop ready` : "Empty"}
        </div>
      </div>

      <div className="looper-timeline" aria-label={`${croppedBars} bar loop`}>
        <div className="looper-grid-lines"><i /><i /><i /><i /><i /><i /><i /><i /></div>
        {hasLoop ? <div className="looper-crop-left" style={{ width: `${cropStartPct}%` }} /> : null}
        {hasLoop ? <div className="looper-crop-right" style={{ width: `${cropEndPct}%` }} /> : null}
        {hasLoop ? <div className="looper-playhead" style={{ left: `${loopProgress}%` }} /> : null}
        <div className="looper-timeline-caption">
          <span>{hasLoop ? `${croppedBars} bar${croppedBars === 1 ? "" : "s"}` : "No loop captured yet"}</span>
          <span>{looping ? "LOOP" : "ONE SHOT"}</span>
        </div>
      </div>

      <div className="looper-controls">
        <div className="looper-primary-controls">
          <button type="button" className={`looper-record-button ${isRecording ? "is-active" : ""}`} onClick={() => setRecording(!isRecording)}>
            <span className="looper-record-dot" /> {isRecording ? "Stop recording" : "Record loop"}
          </button>
          <button type="button" className="btn btn-primary btn-sm" disabled={!hasLoop} onClick={() => setPlaying(!playing)}>
            {playing ? <IconStop size={13} /> : <IconPlay size={13} />} {playing ? "Stop loop" : "Play loop"}
          </button>
          <button type="button" className="btn btn-ghost btn-sm" disabled={!hasLoop && !isRecording} onClick={() => emit(FRONTEND_EVENTS.clearLoop)}>
            <IconTrash size={13} /> Clear
          </button>
          <button type="button" className="btn btn-ghost btn-sm" disabled={!hasLoop} onClick={() => emit(FRONTEND_EVENTS.saveLoop)}>
            <IconSave size={13} /> Save loop
          </button>
        </div>

        <div className="looper-settings">
          <label className="looper-loop-switch">
            <input type="checkbox" checked={looping} onChange={(e) => emit(FRONTEND_EVENTS.setLooperLooping, { enabled: e.target.checked })} />
            <span className="looper-switch" />
            <span>{looping ? "Loop" : "One shot"}</span>
          </label>

          <label className={`looper-click-toggle ${clickEnabled ? "is-on" : ""}`}>
            <input type="checkbox" checked={clickEnabled} onChange={(e) => emit(FRONTEND_EVENTS.setLooperClick, { enabled: e.target.checked })} />
            <IconMetronome size={14} />
            <span>Click {clickEnabled ? "on" : "off"}</span>
          </label>

          <Stepper
            label="Count-in"
            value={countInBeats}
            min={0}
            max={8}
            onChange={(beats) => emit(FRONTEND_EVENTS.setLooperCountIn, { beats })}
            title="Beats of click before the loop capture starts (0 = off)"
          />

          <Stepper
            label="Crop start"
            value={cropStartBars}
            min={0}
            max={!recording && totalBars > 0 ? Math.max(0, totalBars - 1 - cropEndBars) : 0}
            onChange={(bars) => emitCrop(bars, cropEndBars)}
            title="Bars to trim off the start of the loop"
          />

          <Stepper
            label="Crop end"
            value={cropEndBars}
            min={0}
            max={!recording && totalBars > 0 ? Math.max(0, totalBars - 1 - cropStartBars) : 0}
            onChange={(bars) => emitCrop(cropStartBars, bars)}
            title="Bars to trim off the end of the loop"
          />
        </div>
      </div>
    </section>
  );
}
