import { Knob } from "./controls";
import { LevelMeter } from "./LevelMeter";
import { formatTime } from "../utils";
import { FRONTEND_EVENTS, emit } from "../bridge";

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
  const displayLabel = countdown !== null
    ? "Count-in"
    : (isRecording ? "Recording" : (isPlaying ? "Playing" : "Ready"));

  const toggleRecording = () => {
    if (isRecording) emit(FRONTEND_EVENTS.stopRecording);
    else emit(FRONTEND_EVENTS.startRecording);
  };

  const stopPlayback = () => {
    if (isPlaying) emit(FRONTEND_EVENTS.stopPlayback);
  };

  return (
    <section className="transport">
      <button
        type="button"
        className={`record-button ${isRecording ? "is-recording" : ""}`}
        onClick={toggleRecording}
        title={isRecording ? "Stop recording" : "Start recording"}
      >
        <span className="record-dot" />
        {isRecording ? "STOP" : "RECORD"}
      </button>

      <div className={`transport-time ${countdown !== null ? "is-counting-in" : ""}`}>
        <div className="transport-time-value">{displayTime}</div>
        <div className="transport-time-label">{displayLabel}</div>
      </div>

      <div className="transport-meter">
        <LevelMeter level={transport?.inputLevel ?? 0} peak={transport?.inputPeak ?? 0} />
        {isPlaying ? (
          <button type="button" className="stop-playback" onClick={stopPlayback}>
            ■ Stop playback
          </button>
        ) : null}
      </div>

      <div className="transport-metronome">
        <button
          type="button"
          className={`metronome-toggle ${metronomeEnabled ? "is-on" : ""}`}
          onClick={() => onMetronomeChange(!metronomeEnabled)}
          title={metronomeEnabled ? "Click is on during recording and count-in" : "Click is off"}
          aria-pressed={metronomeEnabled}
        >
          <span className="metronome-icon" aria-hidden="true">◆</span>
          Metronome
          <span className="metronome-state">{metronomeEnabled ? "On" : "Off"}</span>
        </button>

        <div className="metronome-knob">
          <Knob
            label="BPM"
            min={40}
            max={240}
            value={bpm}
            onChange={onBpmChange}
            step="1"
            decimals={0}
          />
        </div>

        <label className="count-in-control">
          <span className="count-in-label">Count-in</span>
          <input
            type="number"
            min={0}
            max={8}
            step={1}
            value={countInBeats}
            onChange={(e) => {
              const next = parseInt(e.target.value, 10);
              if (!Number.isNaN(next)) onCountInBeatsChange(Math.max(0, Math.min(8, next)));
            }}
            title="Beats of click before recording starts (0 = off)"
          />
          <span className="count-in-suffix">beats</span>
        </label>
      </div>

      <div className="transport-knob">
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
