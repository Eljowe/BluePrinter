import { Knob } from "./controls";
import { LevelMeter } from "./LevelMeter";

// Header-level monitoring knobs: tempo (shared by the take recorder,
// the looper and the MIDI clock), the input trim (record level) and the
// master monitor output, with the IN / REC / OUT meters that show what
// each stage is doing. The click and MIDI clock toggles live in the sync
// strip beside the recording tabs (SyncControls.jsx), so the header stays
// a clean row of knobs + meters.
function MeterChannel({ label, level, peak, clipped, onResetClip }) {
  return (
    <div className="monitor-meter">
      <LevelMeter
        label={`${label} level`}
        level={level}
        peak={peak}
        clipped={clipped}
        onResetClip={onResetClip}
      />
      <span className="monitor-meter-label">{label}</span>
    </div>
  );
}

export function HeaderControls({
  input,
  onInputChange,
  output,
  onOutputChange,
  bpm,
  onBpmChange,
  dryLevel,
  onDryLevelChange,
  transport,
  onResetClip,
}) {
  const resetClip = (target) => (onResetClip ? () => onResetClip(target) : undefined);
  // The REC bus prints during a take or a loop capture (count-ins included),
  // so the lamp above the REC meter mirrors the Transport/Looper state pills.
  const captureLive = Boolean(
    transport?.recording
    || transport?.preRollActive
    || transport?.looperRecording
    || transport?.looperPreRoll,
  );

  return (
    <div className="header-knobs">
      <Knob
        label="BPM"
        min={40}
        max={240}
        value={bpm}
        onChange={onBpmChange}
        step="1"
        decimals={0}
        title="Tempo — shared by the take recorder, the looper and the MIDI clock"
      />
      <div className="monitor-cell">
        <Knob
          label="Input"
          min={-12}
          max={24}
          value={input}
          onChange={onInputChange}
          step="0.5"
          decimals={1}
          unit="dB"
          title="Input trim (record level) — sets what goes into the chains and the capture"
        />
        <MeterChannel
          label="IN"
          level={transport?.inputLevel}
          peak={transport?.inputPeak}
          clipped={transport?.inputClipped}
          onResetClip={resetClip("input")}
        />
      </div>
      <div className="monitor-cell monitor-cell--meter-only">
        <span
          className={`monitor-lamp ${captureLive ? "is-on" : ""}`}
          title="Lights while a take or loop capture is printing to the REC bus"
        >
          <span className="monitor-lamp-dot" aria-hidden="true" />
          Capture
        </span>
        <MeterChannel
          label="REC"
          level={transport?.recordLevel}
          peak={transport?.recordPeak}
          clipped={transport?.recordClipped}
          onResetClip={resetClip("record")}
        />
      </div>
      <div className="monitor-cell">
        <Knob
          label="Output"
          min={-60}
          max={12}
          value={output}
          onChange={onOutputChange}
          step="0.5"
          decimals={1}
          unit="dB"
          title="Master monitor output — changes what you hear, never what is recorded"
        />
        <MeterChannel
          label="OUT"
          level={transport?.outputLevel}
          peak={transport?.outputPeak}
          clipped={transport?.outputClipped}
          onResetClip={resetClip("output")}
        />
      </div>
      <Knob
        label="Dry"
        min={-60}
        max={0}
        value={dryLevel}
        onChange={onDryLevelChange}
        step="0.5"
        decimals={1}
        unit="dB"
        title="Direct dry pass-through — scales the raw input in the monitor and the capture. Turn it down to hear and record only the chains"
      />
    </div>
  );
}
