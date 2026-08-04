import { IconPlay, IconStop } from "./icons";

// Standalone MIDI clock section: start/stop the free-running clock and
// pick the output device. No recording or looping is needed — the
// clock (and the audible click, subject to the metronome toggle) runs
// until you stop it, so you can audition drum machine patterns before
// recording. The transport and looper sections have their own "MIDI
// clock" toggles that start/stop the clock with a take or a loop
// instead; this free-running toggle is independent of both.
export function MidiClock({ enabled, device, deviceList, bpm, onStartStop, onDeviceChange }) {
  const running = Boolean(enabled);
  const devices = Array.isArray(deviceList) ? deviceList : [];
  const selected = device || devices[0] || "";

  return (
    <section className="midi-clock-section">
      <header className="section-header">
        <div className="section-title">
          <h2>MIDI clock <span className="midi-clock-subtitle">free-run</span></h2>
          <p className="midi-clock-hint">
            Runs free — start it to send Start + 24 ppqn pulses to your drum machine
            without recording, and the click plays along so you can test beats first.
            To have the clock follow a take or a loop instead, use the MIDI clock
            toggles in the transport and looper sections.
          </p>
        </div>
      </header>

      <div className="midi-clock-body">
        <button
          type="button"
          className={`clock-start-stop ${running ? "is-running" : ""}`}
          onClick={onStartStop}
          title={running
            ? "Stop the MIDI clock (sends MIDI Stop)"
            : "Start the MIDI clock (sends MIDI Start + 24 ppqn)"}
          aria-pressed={running}
        >
          {running ? <IconStop size={14} /> : <IconPlay size={14} />}
          <span>{running ? "Stop clock" : "Start clock"}</span>
        </button>

        <div className={`midi-clock-status ${running ? "is-running" : ""}`}>
          <span className={`clock-live-dot ${running ? "is-live" : ""}`} aria-hidden="true" />
          {running
            ? `Clock running at ${Math.round(Number(bpm) || 120)} BPM`
            : "Clock stopped"}
        </div>

        <label className="midi-clock-device">
          <span>MIDI output</span>
          <select
            className="midi-device-select"
            value={selected}
            onChange={(e) => onDeviceChange(e.target.value)}
            disabled={devices.length === 0}
            title="Device that receives the MIDI clock"
          >
            {devices.length === 0 ? (
              <option value="">No MIDI devices found</option>
            ) : (
              devices.map((name) => (
                <option key={name} value={name}>{name}</option>
              ))
            )}
          </select>
        </label>
      </div>
    </section>
  );
}
