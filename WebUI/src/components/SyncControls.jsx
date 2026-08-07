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
function ClickSoundPopover({ transport, onClose }) {
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
  midiClockEnabled,
  onMidiClockChange,
  midiClockOnRecord,
  onMidiClockOnRecordChange,
  midiOutputDevice,
  midiOutputDeviceList,
  onMidiDeviceChange,
}) {
  const [clickOpen, setClickOpen] = useState(false);
  const devices = Array.isArray(midiOutputDeviceList) ? midiOutputDeviceList : [];
  const selectedDevice = midiOutputDevice || devices[0] || "";

  return (
    <div className="sync-strip" role="group" aria-label="Click and MIDI clock">
      <div className="sync-group" role="group" aria-label="Click">
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
          <ClickSoundPopover transport={clickParams} onClose={() => setClickOpen(false)} />
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
    </div>
  );
}
