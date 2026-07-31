import { memo } from "react";

export const MidiLane = memo(function MidiLane({ events = [], position = 0, length = 0 }) {
  const safeLength = Math.max(1, Number(length) || 1);
  const playhead = Math.min(100, Math.max(0, (Number(position) / safeLength) * 100));
  const notes = Array.isArray(events) ? events : [];

  return (
    <div className="midi-lane" aria-label={`${notes.length} recorded MIDI notes`}>
      <div className="midi-lane-grid" aria-hidden="true">
        {Array.from({ length: 9 }, (_, index) => <i key={index} />)}
      </div>
      {notes.map((event, index) => {
        const note = Math.min(127, Math.max(0, Number(event.note) || 0));
        const left = Math.min(100, Math.max(0, (Number(event.position) / safeLength) * 100));
        const top = 5 + ((127 - note) / 127) * 82;
        return <span key={`${event.position}-${event.note}-${index}`} className="midi-lane-note" style={{ left: `${left}%`, top: `${top}%` }} />;
      })}
      <span className="midi-lane-playhead" style={{ left: `${playhead}%` }} aria-hidden="true" />
      <div className="midi-lane-labels" aria-hidden="true"><span>MIDI notes</span><span>{notes.length ? `${notes.length} triggers` : "empty"}</span></div>
    </div>
  );
});
