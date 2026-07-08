import { BypassButton, Knob } from "../components/controls";

export default function Pedalboard({
  drive,
  driveTone,
  driveLevel,
  driveBypassed,
  fuzzDrive,
  fuzzTone,
  fuzzLevel,
  fuzzBypassed,
  compressorAmount,
  compressorBypassed,
  compressorTone,
  compressorLevel,
  reverbSize,
  reverbDamping,
  reverbMix,
  reverbWidth,
  reverbBypassed,
  onDriveChange,
  onDriveToneChange,
  onDriveLevelChange,
  onDriveToggle,
  onFuzzDriveChange,
  onFuzzToneChange,
  onFuzzLevelChange,
  onFuzzToggle,
  onCompressorChange,
  onCompressorToneChange,
  onCompressorLevelChange,
  onCompressorToggle,
  onReverbSizeChange,
  onReverbDampingChange,
  onReverbMixChange,
  onReverbWidthChange,
  onReverbToggle,
}) {
  return (
    <section className="panel-shell fx-shell">
      <p className="section-title">Pedalboard</p>
      <div className="fx-pedal-grid">
        <article className={`fx-pedal-card ${driveBypassed ? "is-bypassed" : ""}`.trim()}>
          <p className="fx-pedal-title">Drive</p>
          <Knob
            className="pedal-compact"
            label="Drive"
            min={0}
            max={24}
            value={drive}
            onChange={onDriveChange}
            unit="dB"
            accent="orange"
          />
          <div className="knob-grid knob-grid-2">
            <Knob
              className="pedal-compact"
              label="Tone"
              min={0}
              max={1}
              step={0.01}
              value={driveTone}
              onChange={onDriveToneChange}
              accent="orange"
            />
            <Knob
              className="pedal-compact"
              label="Level"
              min={-24}
              max={12}
              value={driveLevel}
              onChange={onDriveLevelChange}
              unit="dB"
              accent="orange"
            />
          </div>
          <BypassButton label="Power" enabled={driveBypassed} className="pedal-power" onToggle={onDriveToggle} />
        </article>

        <article className={`fx-pedal-card ${fuzzBypassed ? "is-bypassed" : ""}`.trim()}>
          <p className="fx-pedal-title">Fuzz</p>
          <Knob
            className="pedal-compact"
            label="Fuzz"
            min={0}
            max={24}
            value={fuzzDrive}
            onChange={onFuzzDriveChange}
            unit="dB"
            accent="red"
          />
          <div className="knob-grid knob-grid-2">
            <Knob
              className="pedal-compact"
              label="Tone"
              min={0}
              max={1}
              step={0.01}
              value={fuzzTone}
              onChange={onFuzzToneChange}
              accent="red"
            />
            <Knob
              className="pedal-compact"
              label="Level"
              min={-24}
              max={12}
              value={fuzzLevel}
              onChange={onFuzzLevelChange}
              unit="dB"
              accent="red"
            />
          </div>
          <BypassButton
            label="Power"
            enabled={fuzzBypassed}
            className="pedal-power"
            onToggle={onFuzzToggle}
          />
        </article>

        <article className={`fx-pedal-card fx-pedal-card-secondary ${compressorBypassed ? "is-bypassed" : ""}`.trim()}>
          <p className="fx-pedal-title">Compressor</p>
          <Knob
            className="pedal-compact"
            label="Comp"
            min={0}
            max={24}
            value={compressorAmount}
            onChange={onCompressorChange}
            unit="dB"
            accent="blue"
          />
          <div className="knob-grid knob-grid-2">
            <Knob
              className="pedal-compact"
              label="Tone"
              min={0}
              max={1}
              step={0.01}
              value={compressorTone}
              onChange={onCompressorToneChange}
              accent="blue"
            />
            <Knob
              className="pedal-compact"
              label="Level"
              min={-12}
              max={24}
              value={compressorLevel}
              onChange={onCompressorLevelChange}
              unit="dB"
              accent="blue"
            />
          </div>
          <BypassButton
            label="Power"
            enabled={compressorBypassed}
            className="pedal-power"
            onToggle={onCompressorToggle}
          />
        </article>

        <article className={`fx-pedal-card ${reverbBypassed ? "is-bypassed" : ""}`.trim()}>
          <p className="fx-pedal-title">Reverb</p>
          <div className="knob-grid knob-grid-2">
            <Knob
              className="pedal-compact"
              label="Size"
              min={0}
              max={1}
              step={0.01}
              value={reverbSize}
              onChange={onReverbSizeChange}
              accent="purple"
            />
            <Knob
              className="pedal-compact"
              label="Damping"
              min={0}
              max={1}
              step={0.01}
              value={reverbDamping}
              onChange={onReverbDampingChange}
              accent="purple"
            />
            <Knob
              className="pedal-compact"
              label="Mix"
              min={0}
              max={1}
              step={0.01}
              value={reverbMix}
              onChange={onReverbMixChange}
              accent="purple"
            />
            <Knob
              className="pedal-compact"
              label="Width"
              min={0}
              max={1}
              step={0.01}
              value={reverbWidth}
              onChange={onReverbWidthChange}
              accent="purple"
            />
          </div>
          <BypassButton
            label="Power"
            enabled={reverbBypassed}
            className="pedal-power"
            onToggle={onReverbToggle}
          />
        </article>
      </div>
    </section>
  );
}
