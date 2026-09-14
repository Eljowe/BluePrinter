import { useState } from "react";
import { IconMetronome, IconX } from "./icons";
import { emit, FRONTEND_EVENTS } from "../bridge";

const CLICK_DEFAULTS = {
  pitch: 1000,
  accentPitch: 1500,
  decay: 90,
  volume: 0.35,
  accentVolume: 0.5,
  noise: 0.1,
};

function ClickSlider({ label, min, max, step, value, onChange, format, title }) {
  return (
    <label className="click-slider" title={title}>
      <span className="click-slider-label">{label}</span>
      <input
        type="range"
        min={min}
        max={max}
        step={step}
        value={value}
        onChange={(e) => onChange(Number(e.target.value))}
      />
      <span className="click-slider-value">{format ? format(value) : value}</span>
    </label>
  );
}

// Click sound tuning for the metronome click — one global sound shared
// by the take recorder, the looper and the free-running clock. Lives in
// the sync strip next to the click toggles so both recording sections
// stay lean.
function ClickSoundPopover({ transport, beats, accents, onAccentsChange, onClose }) {
  // Optimistic local state: values are initialized from the last
  // backend push and every change emits the full parameter set. The
  // popup never re-binds to the 30 Hz transport snapshot, so dragging
  // a slider is never fighting incoming updates.
  const [draft, setDraft] = useState(() => ({
    pitch: Number(transport?.clickPitch ?? CLICK_DEFAULTS.pitch),
    accentPitch: Number(transport?.clickAccentPitch ?? CLICK_DEFAULTS.accentPitch),
    decay: Number(transport?.clickDecay ?? CLICK_DEFAULTS.decay),
    volume: Number(transport?.clickVolume ?? CLICK_DEFAULTS.volume),
    accentVolume: Number(transport?.clickAccentVolume ?? CLICK_DEFAULTS.accentVolume),
    noise: Number(transport?.clickNoise ?? CLICK_DEFAULTS.noise),
  }));

  const update = (key, value) => {
    const next = { ...draft, [key]: value };
    setDraft(next);
    emit(FRONTEND_EVENTS.setClickParams, next);
  };

  const reset = () => {
    setDraft(CLICK_DEFAULTS);
    emit(FRONTEND_EVENTS.setClickParams, CLICK_DEFAULTS);
  };

  return (
    <div className="click-popover" role="dialog" aria-label="Click sound settings">
      <div className="click-popover-header">
        <span className="click-popover-title">Click sound</span>
        <div className="click-popover-actions">
          <button type="button" className="btn btn-ghost btn-sm" onClick={reset} title="Reset to defaults">
            Reset
          </button>
          <button type="button" className="icon-btn" onClick={onClose} title="Close" aria-label="Close">
            <IconX size={13} />
          </button>
        </div>
      </div>

      <ClickSlider
        label="Tick pitch"
        min={400} max={3000} step={50}
        value={draft.pitch}
        onChange={(v) => update("pitch", v)}
        format={(v) => `${v} Hz`}
      />
      <ClickSlider
        label="Accent pitch"
        min={400} max={3000} step={50}
        value={draft.accentPitch}
        onChange={(v) => update("accentPitch", v)}
        format={(v) => `${v} Hz`}
      />
      <ClickSlider
        label="Snap"
        min={20} max={300} step={5}
        value={draft.decay}
        onChange={(v) => update("decay", v)}
        format={(v) => `${v}/s`}
        title="How fast the click dies away — higher is snappier"
      />
      <ClickSlider
        label="Tick vol"
        min={0} max={1} step={0.05}
        value={draft.volume}
        onChange={(v) => update("volume", v)}
        format={(v) => v.toFixed(2)}
      />
      <ClickSlider
        label="Accent vol"
        min={0} max={1} step={0.05}
        value={draft.accentVolume}
        onChange={(v) => update("accentVolume", v)}
        format={(v) => v.toFixed(2)}
      />
      <ClickSlider
        label="Attack noise"
        min={0} max={0.3} step={0.01}
        value={draft.noise}
        onChange={(v) => update("noise", v)}
        format={(v) => v.toFixed(2)}
      />

      <div className="click-accents" role="group" aria-label="Bar accents">
        <span className="click-slider-label">Bar accents</span>
        <div className="click-accents-row">
          {Array.from({ length: beats }).map((_, i) => (
            <button
              key={i}
              type="button"
              className={`click-accent-step ${accents[i] ? "is-on" : ""}`}
              aria-pressed={Boolean(accents[i])}
              aria-label={`Beat ${i + 1} accent`}
              title={`Accent beat ${i + 1}`}
              onClick={() => {
                const next = Array.from({ length: beats }, (_, j) => Boolean(accents[j]));
                next[i] = !next[i];
                onAccentsChange(next);
              }}
            >
              {i + 1}
            </button>
          ))}
          <button
            type="button"
            className="btn btn-ghost btn-sm"
            onClick={() => onAccentsChange(Array.from({ length: beats }, (_, i) => i === 0))}
            title="Accent the first beat only"
          >
            Reset
          </button>
        </div>
      </div>
    </div>
  );
}

// The sync strip: click + MIDI clock controls as one compact bar beside
// the recording tabs. Every recording path (count-ins, takes, loop
// captures, the free-running clock) shares these — the transport and
// looper sections stay lean.
export function SyncControls({
  metronomeEnabled,
  onMetronomeChange,
  clickDuringCapture,
  onClickDuringCaptureChange,
  clickParams,
  clickSubdivision,
  onClickSubdivisionChange,
  clickAccents,
  onClickAccentsChange,
  timeSignatureNumerator,
  timeSignatureDenominator,
  onTimeSignatureChange,
  midiClockEnabled,
  onMidiClockChange,
  midiClockOnRecord,
  onMidiClockOnRecordChange,
  midiOutputDevice,
  midiOutputDeviceList,
  onMidiDeviceChange,
  captureStemsEnabled,
  onCaptureStemsChange,
}) {
  const [clickOpen, setClickOpen] = useState(false);
  const devices = Array.isArray(midiOutputDeviceList) ? midiOutputDeviceList : [];
  const selectedDevice = midiOutputDevice || devices[0] || "";
  const numerator = Number(timeSignatureNumerator ?? 4);
  const denominator = Number(timeSignatureDenominator ?? 4);
  const meterValue = `${numerator}/${denominator}`;
  const accents = Array.isArray(clickAccents) ? clickAccents : [];
  // Keep a value restored from old/edited state visible even when it isn't one
  // of the presets (a controlled <select> would otherwise render blank).
  const METERS = ["2/4", "3/4", "4/4", "5/4", "6/8", "7/8", "9/8", "12/8"];
  const meterOptions = METERS.includes(meterValue) ? METERS : [meterValue, ...METERS];

  return (
    <div className="sync-strip" role="group" aria-label="Click and MIDI clock">
      <div className="sync-group" role="group" aria-label="Click">
        <label
          className="sync-device"
          title="Time signature — beats per bar / note value. BPM stays a quarter-note tempo; the click accents beat 1 of each bar."
        >
          <span className="sync-device-label">Meter</span>
          <select
            className="midi-device-select meter-select"
            value={meterValue}
            onChange={(e) => {
              const [n, d] = e.target.value.split("/").map(Number);
              onTimeSignatureChange(n, d);
            }}
          >
            {meterOptions.map((m) => (
              <option key={m} value={m}>{m}</option>
            ))}
          </select>
        </label>

        <label className="sync-device" title="Click subdivisions — soft extra clicks between the beats">
          <span className="sync-device-label">Subdiv</span>
          <select
            className="midi-device-select meter-select"
            value={String(clickSubdivision ?? 0)}
            onChange={(e) => onClickSubdivisionChange(Number(e.target.value))}
          >
            <option value="0">Off</option>
            <option value="2">8ths</option>
            <option value="3">Triplets</option>
            <option value="4">16ths</option>
          </select>
        </label>

        <button
          type="button"
          className={`sync-toggle ${metronomeEnabled ? "is-on" : ""}`}
          onClick={() => onMetronomeChange(!metronomeEnabled)}
          title={metronomeEnabled
            ? "Click is on — count-ins, takes, loop captures and the free-running clock"
            : "Click is off — no metronome click anywhere"}
          aria-pressed={metronomeEnabled}
        >
          <IconMetronome size={13} />
          Click
        </button>

        <button
          type="button"
          className={`sync-toggle ${clickDuringCapture ? "is-on" : ""}`}
          onClick={() => onClickDuringCaptureChange(!clickDuringCapture)}
          title={clickDuringCapture
            ? "Click plays through the whole take and loop capture. Turn on for count-in only."
            : "Click only during count-ins — silent while takes and loop captures record."}
          aria-pressed={clickDuringCapture}
        >
          <span className="sync-dot" aria-hidden="true" />
          Click: capture
        </button>

        <button
          type="button"
          className="sync-text"
          onClick={() => setClickOpen((v) => !v)}
          title="Tune the click sound (pitch, snap, volume)"
          aria-expanded={clickOpen}
        >
          Click sound
        </button>
        {clickOpen ? (
          <ClickSoundPopover
            transport={clickParams}
            beats={numerator}
            accents={accents}
            onAccentsChange={onClickAccentsChange}
            onClose={() => setClickOpen(false)}
          />
        ) : null}
      </div>

      <span className="sync-divider" aria-hidden="true" />

      <div className="sync-group" role="group" aria-label="MIDI clock">
        <button
          type="button"
          className={`sync-toggle ${midiClockEnabled ? "is-live" : ""}`}
          onClick={() => onMidiClockChange(!midiClockEnabled)}
          title={midiClockEnabled
            ? midiClockOnRecord
              ? "MIDI clock starts with takes and loop captures — silent while idle"
              : "MIDI clock is running — Start + 24 ppqn pulses to the drum machine (free-run; takes and loop captures ride it)"
            : "Start the MIDI clock — Start + 24 ppqn pulses to the drum machine"}
          aria-pressed={midiClockEnabled}
        >
          <span className={`sync-dot ${midiClockEnabled ? "is-live" : ""}`} aria-hidden="true" />
          Clock
        </button>

        <button
          type="button"
          className={`sync-toggle ${midiClockOnRecord ? "is-on" : ""}`}
          onClick={() => onMidiClockOnRecordChange(!midiClockOnRecord)}
          title={midiClockOnRecord
            ? "Clock runs only while a take or loop records — silent while idle. Takes effect when the MIDI clock is on."
            : "Clock free-runs whenever the MIDI clock is on. Turn on to start the clock only with takes and loop captures."}
          aria-pressed={midiClockOnRecord}
        >
          <span className="sync-dot" aria-hidden="true" />
          On record
        </button>

        <label className="sync-device" title="Device that receives the MIDI clock">
          <span className="sync-device-label">MIDI out</span>
          <select
            className="midi-device-select"
            value={selectedDevice}
            onChange={(e) => onMidiDeviceChange(e.target.value)}
            disabled={devices.length === 0}
          >
            {devices.length === 0 ? (
              <option value="">No MIDI devices</option>
            ) : (
              devices.map((name) => (
                <option key={name} value={name}>{name}</option>
              ))
            )}
          </select>
        </label>
      </div>

      <span className="sync-divider" aria-hidden="true" />

      <div className="sync-group" role="group" aria-label="Stems">
        <button
          type="button"
          className={`sync-toggle ${captureStemsEnabled ? "is-on" : ""}`}
          onClick={() => onCaptureStemsChange(!captureStemsEnabled)}
          title={captureStemsEnabled
            ? "Stems on — the next fresh take or loop capture also records one stem per record-on-capture chain plus the dry input, ready to export."
            : "Capture stems so a fresh take or loop can be exported as one file per chain (plus dry) that sum back to the mix."}
          aria-pressed={captureStemsEnabled}
        >
          <span className="sync-dot" aria-hidden="true" />
          Stems
        </button>
      </div>
    </div>
  );
}
