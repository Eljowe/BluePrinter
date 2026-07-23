import { useCallback, useEffect, useRef, useState } from "react";
import { Knob } from "./controls";
import { LevelMeter } from "./LevelMeter";
import { IconMetronome, IconStop } from "./icons";
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

export function Transport({
  transport,
  gain,
  onGainChange,
  metronomeEnabled,
  bpm,
  countInBeats,
  onMetronomeChange,
  onBpmChange,
  onCountInBeatsChange,
}) {
  const isPreRoll = Boolean(transport?.preRollActive);
  const isRecording = Boolean(transport?.recording) || isPreRoll;
  const isPlaying = (transport?.playingSnippetId ?? -1) >= 0;
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
    : (isRecording ? "recording" : (isPlaying ? "playing" : "ready"));

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
        <button
          type="button"
          className={`metronome-toggle ${metronomeEnabled ? "is-on" : ""}`}
          onClick={() => onMetronomeChange(!metronomeEnabled)}
          title={metronomeEnabled ? "Click is on during recording and count-in" : "Click is off"}
          aria-pressed={metronomeEnabled}
        >
          <IconMetronome size={15} />
          <span className="metronome-state">{metronomeEnabled ? "Click on" : "Click off"}</span>
        </button>

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
          label="Gain"
          min={0}
          max={1}
          value={gain}
          onChange={onGainChange}
          step="0.01"
          decimals={2}
        />
      </div>
    </section>
  );
}
