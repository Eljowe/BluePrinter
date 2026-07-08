import { BypassButton, Knob } from "../components/controls";

export default function Pedalboard({
  tunerBypassed,
  tunerNote,
  tunerCents,
  tunerFrequency,
  tunerLevel,
  gateThreshold,
  gateBypassed,
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
  octaveTranspose,
  octaveBypassed,
  doublerMix,
  doublerDelay,
  doublerDetune,
  doublerBypassed,
  delayMix,
  delayTimeL,
  delayTimeR,
  delayFeedback,
  delayModeIsDual,
  delayBypassed,
  reverbSize,
  reverbDamping,
  reverbMix,
  reverbWidth,
  reverbBypassed,
  onTunerToggle,
  onGateThresholdChange,
  onGateToggle,
  onCompressorChange,
  onCompressorToneChange,
  onCompressorLevelChange,
  onCompressorToggle,
  onDriveChange,
  onDriveToneChange,
  onDriveLevelChange,
  onDriveToggle,
  onFuzzDriveChange,
  onFuzzToneChange,
  onFuzzLevelChange,
  onFuzzToggle,
  onOctaveTransposeChange,
  onOctaveToggle,
  onDoublerMixChange,
  onDoublerDelayChange,
  onDoublerDetuneChange,
  onDoublerToggle,
  onDelayMixChange,
  onDelayTimeLChange,
  onDelayTimeRChange,
  onDelayFeedbackChange,
  onDelayModeToggle,
  onDelayToggle,
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
        <article className={`fx-pedal-card ${tunerBypassed ? "is-bypassed" : ""}`.trim()}>
          <p className="fx-pedal-title">Tuner</p>
          <div className="tuner-display">
            <div className={`tuner-note ${tunerNote ? "" : "is-muted"}`.trim()}>
              {tunerNote || "—"}
            </div>
            <div className="tuner-cents-track">
              <div className="tuner-cents-center" />
              <div
                className={`tuner-cents-needle ${Math.abs(tunerCents) > 15 ? "is-very-off" : ""}`.trim()}
                style={{ left: `${50 + Math.max(-50, Math.min(50, tunerCents))}%` }}
              />
            </div>
            <div className="tuner-cents-readout">
              {tunerNote
                ? `${tunerCents > 0 ? "+" : ""}${tunerCents.toFixed(1)} ¢ · ${tunerFrequency.toFixed(1)} Hz`
                : tunerBypassed
                  ? "Tuner bypassed"
                  : "Play a note"}
            </div>
            <div className="tuner-level-bar">
              <div
                className="tuner-level-fill"
                style={{ width: `${Math.max(0, Math.min(100, ((tunerLevel + 60) / 60) * 100))}%` }}
              />
            </div>
          </div>
          <BypassButton
            label="Power"
            enabled={tunerBypassed}
            className="pedal-power"
            onToggle={onTunerToggle}
          />
        </article>

        <article className={`fx-pedal-card ${gateBypassed ? "is-bypassed" : ""}`.trim()}>
          <p className="fx-pedal-title">Gate</p>
          <Knob
            className="pedal-compact"
            label="Threshold"
            min={-60}
            max={0}
            value={gateThreshold}
            onChange={onGateThresholdChange}
            unit="dB"
            accent="lime"
          />
          <BypassButton
            label="Power"
            enabled={gateBypassed}
            className="pedal-power"
            onToggle={onGateToggle}
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

        <article className={`fx-pedal-card ${octaveBypassed ? "is-bypassed" : ""}`.trim()}>
          <p className="fx-pedal-title">Octave</p>
          <div className="knob-grid knob-grid-1">
            <Knob
              className="pedal-compact"
              label="Transpose"
              min={-12}
              max={12}
              step={1}
              value={octaveTranspose}
              onChange={onOctaveTransposeChange}
              unit="st"
              accent="magenta"
              decimals={0}
            />
          </div>
          <BypassButton
            label="Power"
            enabled={octaveBypassed}
            className="pedal-power"
            onToggle={onOctaveToggle}
          />
        </article>

        <article className={`fx-pedal-card ${doublerBypassed ? "is-bypassed" : ""}`.trim()}>
          <p className="fx-pedal-title">Doubler</p>
          <Knob
            className="pedal-compact"
            label="Mix"
            min={0}
            max={1}
            step={0.01}
            value={doublerMix}
            onChange={onDoublerMixChange}
            accent="teal"
          />
          <div className="knob-grid knob-grid-2">
            <Knob
              className="pedal-compact"
              label="Delay"
              min={0}
              max={100}
              step={0.5}
              value={doublerDelay}
              onChange={onDoublerDelayChange}
              unit="ms"
              decimals={0}
              accent="teal"
            />
            <Knob
              className="pedal-compact"
              label="Detune"
              min={-50}
              max={50}
              step={1}
              value={doublerDetune}
              onChange={onDoublerDetuneChange}
              unit="ct"
              decimals={0}
              accent="teal"
            />
          </div>
          <BypassButton
            label="Power"
            enabled={doublerBypassed}
            className="pedal-power"
            onToggle={onDoublerToggle}
          />
        </article>

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

        <article
          className={`fx-pedal-card fx-pedal-card-wide ${delayBypassed ? "is-bypassed" : ""}`.trim()}
        >
          <p className="fx-pedal-title">Delay</p>
          <div className="knob-grid knob-grid-3">
            <Knob
              className="pedal-compact"
              label="Mix"
              min={0}
              max={1}
              step={0.01}
              value={delayMix}
              onChange={onDelayMixChange}
              accent="cyan"
            />
            <Knob
              className="pedal-compact"
              label="Feedback"
              min={0}
              max={0.95}
              step={0.01}
              value={delayFeedback}
              onChange={onDelayFeedbackChange}
              accent="cyan"
            />
            <div className="pedal-mode-toggle">
              <div className="pedal-mode-option">
                <button
                  type="button"
                  className={`pedal-mode-circle ${!delayModeIsDual ? "active" : ""}`.trim()}
                  onClick={() => { if (delayModeIsDual) onDelayModeToggle(); }}
                  aria-label="Single mode"
                />
                <span className="pedal-mode-label">Single</span>
              </div>
              <div className="pedal-mode-option">
                <button
                  type="button"
                  className={`pedal-mode-circle ${delayModeIsDual ? "active" : ""}`.trim()}
                  onClick={() => { if (!delayModeIsDual) onDelayModeToggle(); }}
                  aria-label="Dual mode"
                />
                <span className="pedal-mode-label">Dual</span>
              </div>
            </div>
          </div>
          <div className="knob-grid knob-grid-2">
            <Knob
              className="pedal-compact"
              label="Time L"
              min={0}
              max={2000}
              step={1}
              value={delayTimeL}
              onChange={onDelayTimeLChange}
              unit="ms"
              decimals={0}
              accent="cyan"
            />
            <Knob
              className="pedal-compact"
              label="Time R"
              min={0}
              max={2000}
              step={1}
              value={delayTimeR}
              onChange={delayModeIsDual ? onDelayTimeRChange : undefined}
              unit="ms"
              decimals={0}
              accent="cyan"
              disabled={!delayModeIsDual}
            />
          </div>
          <BypassButton
            label="Power"
            enabled={delayBypassed}
            className="pedal-power"
            onToggle={onDelayToggle}
          />
        </article>
      </div>
    </section>
  );
}
