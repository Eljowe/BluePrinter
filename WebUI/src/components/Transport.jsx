import { useCallback, useEffect, useRef, useState } from "react";
import { Knob } from "./controls";
import { LevelMeter } from "./LevelMeter";
import { IconMetronome, IconStop, IconX } from "./icons";
import { formatTime } from "../utils";
import { FRONTEND_EVENTS, emit } from "../bridge";

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

// One row of the "Click sound" popover: label + range slider + value.
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

const CLICK_DEFAULTS = {
  pitch: 1000,
  accentPitch: 1500,
  decay: 90,
  volume: 0.35,
  accentVolume: 0.5,
  noise: 0.1,
};

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

export function Transport({
  transport,
  gain,
  onGainChange,
  playbackVolume,
  onPlaybackVolumeChange,
  metronomeEnabled,
  bpm,
  countInBeats,
  dryLevel,
  midiClockEnabled,
  takeMidiClock,
  clickDuringTake,
  onMetronomeChange,
  onBpmChange,
  onCountInBeatsChange,
  onDryLevelChange,
  onTakeMidiClockChange,
  onClickDuringTakeChange,
}) {
  const isPreRoll = Boolean(transport?.preRollActive);
  const isRecording = Boolean(transport?.recording) || isPreRoll;
  const isPlaying = (transport?.playingSnippetId ?? -1) >= 0;
  const [clickOpen, setClickOpen] = useState(false);
  const recordingSeconds = isRecording && transport?.recordingSampleRate > 0
    ? (transport.recordingLength ?? 0) / transport.recordingSampleRate
    : 0;

  // Compute the count-in countdown from the current transport position.
  // Beat boundaries are spaced at 60/bpm seconds; we want to show "4", "3",
  // "2", "1" then start recording.
  let countdown = null;
  if (isPreRoll && bpm > 0 && transport?.recordingSampleRate > 0) {
    const samplesPerBeat = 60.0 / bpm * transport.recordingSampleRate;
    const currentBeat = Math.floor((transport.transportPosition ?? 0) / samplesPerBeat);
    countdown = Math.max(1, countInBeats - currentBeat);
  }

  const displayTime = countdown !== null ? String(countdown) : formatTime(recordingSeconds);

  const status = countdown !== null
    ? "count-in"
    : (isRecording ? "recording" : (isPlaying ? "playing" : (midiClockEnabled ? "clock" : "ready")));

  const toggleRecording = () => {
    if (isRecording) emit(FRONTEND_EVENTS.stopRecording);
    else emit(FRONTEND_EVENTS.startRecording);
  };

  const stopPlayback = () => {
    if (isPlaying) emit(FRONTEND_EVENTS.stopPlayback);
  };

  return (
    <section className={`transport is-${status}`}>
      <div className="transport-rec">
        <button
          type="button"
          className={`rec-button ${isRecording ? "is-recording" : ""}`}
          onClick={toggleRecording}
          title={isRecording ? "Stop recording" : "Start recording"}
          aria-pressed={isRecording}
          aria-label={isRecording ? "Stop recording" : "Start recording"}
        >
          <span className="rec-glyph" aria-hidden="true" />
        </button>

        <div className={`transport-readout ${countdown !== null ? "is-counting-in" : ""}`}>
          <div className="transport-time-value">{displayTime}</div>
          <div className="transport-status">
            <span className="transport-status-dot" aria-hidden="true" />
            {status}
          </div>
        </div>
      </div>

      <div className="transport-meter">
        <LevelMeter level={transport?.inputLevel ?? 0} peak={transport?.inputPeak ?? 0} />
        {isPlaying ? (
          <button type="button" className="btn btn-ghost btn-sm stop-playback" onClick={stopPlayback}>
            <IconStop size={12} />
            Stop playback
          </button>
        ) : null}
      </div>

      <div className="transport-group">
        <div className="click-pair">
          <button
            type="button"
            className={`metronome-toggle ${metronomeEnabled ? "is-on" : ""}`}
            onClick={() => onMetronomeChange(!metronomeEnabled)}
            title={metronomeEnabled
              ? clickDuringTake
                ? "Click is on during the count-in and the take"
                : "Click is on during the count-in only (silent through the take)"
              : "Click is off"}
            aria-pressed={metronomeEnabled}
          >
            <IconMetronome size={15} />
            <span className="metronome-state">{metronomeEnabled ? "Click on" : "Click off"}</span>
          </button>
          <button
            type="button"
            className={`metronome-toggle ${!clickDuringTake ? "is-on" : ""}`}
            onClick={() => onClickDuringTakeChange(!clickDuringTake)}
            title={clickDuringTake
              ? "Click plays through the whole take. Turn on for count-in only (click stops when recording starts)."
              : "Click only during the count-in — silent while the take records."}
            aria-pressed={!clickDuringTake}
          >
            <span className="metronome-state">
              {clickDuringTake ? "Click: take" : "Click: count-in only"}
            </span>
          </button>
          <button
            type="button"
            className={`metronome-toggle ${takeMidiClock ? "is-on" : ""}`}
            onClick={() => onTakeMidiClockChange(!takeMidiClock)}
            title={takeMidiClock
              ? "MIDI clock runs while this take records (and its count-in) and stops when the take stops"
              : "Send MIDI clock with the take — the drum machine starts at the count-in and stops when the take stops"}
            aria-pressed={takeMidiClock}
          >
            <span className={`clock-live-dot ${takeMidiClock ? "is-live" : ""}`} aria-hidden="true" />
            <span className="metronome-state">
              {takeMidiClock ? "Clock w/ take" : "MIDI clock"}
            </span>
          </button>
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
            <ClickSoundPopover transport={transport} onClose={() => setClickOpen(false)} />
          ) : null}
        </div>

        <Knob
          label="BPM"
          min={40}
          max={240}
          value={bpm}
          onChange={onBpmChange}
          step="1"
          decimals={0}
        />

        <label className="count-in-control">
          <span className="count-in-label">Count-in</span>
          <NumberInput
            className="count-in-field"
            min={0}
            max={8}
            step={1}
            value={countInBeats}
            onChange={onCountInBeatsChange}
            suffix="beats"
            title="Beats of click before recording starts (0 = off)"
          />
        </label>
      </div>

      <div className="transport-group transport-gain">
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
    </section>
  );
}
