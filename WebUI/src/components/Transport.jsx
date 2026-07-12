import { Knob } from "./controls";
import { LevelMeter } from "./LevelMeter";
import { formatTime } from "../utils";
import { FRONTEND_EVENTS, emit } from "../bridge";

export function Transport({ transport, gain, onGainChange }) {
  const isRecording = Boolean(transport?.recording);
  const isPlaying = (transport?.playingSnippetId ?? -1) >= 0;
  const recordingSeconds = isRecording && transport?.recordingSampleRate > 0
    ? (transport.recordingLength ?? 0) / transport.recordingSampleRate
    : 0;

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

      <div className="transport-time">
        <div className="transport-time-value">{formatTime(recordingSeconds)}</div>
        <div className="transport-time-label">{isRecording ? "Recording" : (isPlaying ? "Playing" : "Ready")}</div>
      </div>

      <div className="transport-meter">
        <LevelMeter level={transport?.inputLevel ?? 0} peak={transport?.inputPeak ?? 0} />
        {isPlaying ? (
          <button type="button" className="stop-playback" onClick={stopPlayback}>
            ■ Stop playback
          </button>
        ) : null}
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
