import { FRONTEND_EVENTS, emit } from "../bridge";
import { IconPlay, IconStop, IconTrash } from "./icons";

export function MidiSequencer({ transport }) {
  const events = Number(transport?.midiSequencerEventCount ?? 0);
  const recording = Boolean(transport?.midiSequencerRecording);
  const playing = Boolean(transport?.midiSequencerPlaying);
  const looping = transport?.midiSequencerLooping !== false;
  const hasSequence = events > 0 || Number(transport?.audioLoopLength ?? 0) > 0;
  const loopProgress = transport?.audioLoopLength > 0
    ? Math.min(100, Math.max(0, (transport.audioLoopPosition / transport.audioLoopLength) * 100))
    : 0;

  const setRecording = (enabled) => emit(FRONTEND_EVENTS.setMidiSequencerRecording, { enabled });
  const setPlaying = (enabled) => emit(FRONTEND_EVENTS.setMidiSequencerPlaying, { enabled });

  return (
    <section className={`midi-sequencer ${recording ? "is-recording" : ""} ${playing ? "is-playing" : ""}`}>
      <div className="sequencer-header">
        <div>
          <div className="eyebrow">Audio + MIDI looper</div>
          <h2>Capture a loop. Play guitar over it.</h2>
          <p>Record guitar and MIDI together, then loop both while you experiment.</p>
        </div>
        <div className="sequencer-state" aria-live="polite">
          <span className="sequencer-state-dot" />
          {recording ? "Recording MIDI" : playing ? "Loop playing" : hasSequence ? "Sequence ready" : "Empty"}
        </div>
      </div>

      <div className="sequencer-timeline" aria-label={`${events} MIDI events in sequence`}>
        <div className="sequencer-grid-lines"><i /><i /><i /><i /><i /><i /><i /><i /></div>
        {hasSequence ? <div className="sequencer-event-density"><span /></div> : null}
        {hasSequence ? <div className="sequencer-playhead" style={{ left: `${loopProgress}%` }} /> : null}
        <div className="sequencer-timeline-caption">
          <span>{hasSequence ? `${events} MIDI events + audio` : "No loop captured yet"}</span>
          <span>{looping ? "LOOP ON" : "ONE SHOT"}</span>
        </div>
      </div>

      <div className="sequencer-controls">
        <div className="sequencer-primary-controls">
          <button type="button" className={`sequencer-record-button ${recording ? "is-active" : ""}`} onClick={() => setRecording(!recording)}>
            <span className="sequencer-record-dot" /> {recording ? "Stop recording" : "Record loop"}
          </button>
          <button type="button" className="btn btn-primary btn-sm" disabled={!hasSequence} onClick={() => setPlaying(!playing)}>
            {playing ? <IconStop size={13} /> : <IconPlay size={13} />} {playing ? "Stop loop" : "Play loop"}
          </button>
          <button type="button" className="btn btn-ghost btn-sm" disabled={!hasSequence} onClick={() => emit(FRONTEND_EVENTS.clearMidiSequence)}>
            <IconTrash size={13} /> Clear
          </button>
          <button type="button" className="btn btn-ghost btn-sm" disabled={!hasSequence} onClick={() => emit(FRONTEND_EVENTS.saveMidiSequence)}>
            Save MIDI
          </button>
          <button type="button" className="btn btn-ghost btn-sm" onClick={() => emit(FRONTEND_EVENTS.loadMidiSequence)}>
            Load MIDI
          </button>
        </div>
        <label className="sequencer-loop-switch">
          <input type="checkbox" checked={looping} onChange={(e) => emit(FRONTEND_EVENTS.setMidiSequencerLooping, { enabled: e.target.checked })} />
          <span className="sequencer-switch" />
          <span>Loop sequence</span>
        </label>
        <label className="sequencer-quantize-control">
          <span>Quantize</span>
          <select value={transport?.midiQuantizationDivision ?? 0} onChange={(e) => emit(FRONTEND_EVENTS.setMidiQuantization, { division: Number(e.target.value) })}>
            <option value={0}>Off</option>
            <option value={4}>1/4</option>
            <option value={8}>1/8</option>
            <option value={16}>1/16</option>
            <option value={32}>1/32</option>
          </select>
        </label>
      </div>
    </section>
  );
}
