import { useState } from "react";
import { Knob } from "./controls";
import { IconX } from "./icons";
import { emit, FRONTEND_EVENTS } from "../bridge";

const CLICK_DEFAULTS = {
  pitch: 1000,
  accentPitch: 1500,
  decay: 90,
  volume: 0.35,
  accentVolume: 0.5,
  noise: 0.1,
};

function ClickSlider({ label, min, max, step, value, onChange, format }) {
  return (
    <label className="click-slider">
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
// the header next to the monitoring knobs so both recording sections
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

// Global monitoring + click controls: Dry / Gain / Play Vol knobs and
// the Click sound popover. Shared by every recording/playback path, so
// they live in the plugin header rather than the recording block.
export function HeaderControls({
  gain,
  onGainChange,
  playbackVolume,
  onPlaybackVolumeChange,
  dryLevel,
  onDryLevelChange,
  clickParams,
}) {
  const [clickOpen, setClickOpen] = useState(false);

  return (
    <div className="app-header-controls">
      <div className="header-click-sound">
        <button
          type="button"
          className="click-sound-toggle"
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

      <div className="header-knobs">
        <Knob
          label="Dry"
          min={0}
          max={1}
          value={dryLevel}
          onChange={onDryLevelChange}
          step="0.01"
          decimals={2}
        />
        <Knob
          label="Gain"
          min={0}
          max={1}
          value={gain}
          onChange={onGainChange}
          step="0.01"
          decimals={2}
        />
        <Knob
          label="Play Vol"
          min={0}
          max={1}
          value={playbackVolume}
          onChange={onPlaybackVolumeChange}
          step="0.01"
          decimals={2}
        />
      </div>
    </div>
  );
}
