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
  overdriveDrive,
  overdriveTone,
  overdriveLevel,
  overdriveBypassed,
  fuzzDrive,
  fuzzTone,
  fuzzLevel,
  fuzzBypassed,
  synthFuzzMix,
  synthFuzzDelay,
  synthFuzzDetune,
  synthFuzzDrive,
  synthFuzzLevel,
  synthFuzzBypassed,
  compressorAmount,
  compressorBypassed,
  compressorTone,
  compressorLevel,
  octaveTranspose,
  octaveMix,
  octaveTone,
  octaveMonoDetector,
  octaveBypassed,
  doublerMix,
  doublerDelay,
  doublerDetune,
  doublerBypassed,
  tremoloSpeed,
  tremoloDepth,
  tremoloLfoIndex,
  tremoloStereoPhase,
  tremoloBypassed,
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
  onOverdriveDriveChange,
  onOverdriveToneChange,
  onOverdriveLevelChange,
  onOverdriveToggle,
  onFuzzDriveChange,
  onFuzzToneChange,
  onFuzzLevelChange,
  onFuzzToggle,
  onSynthFuzzMixChange,
  onSynthFuzzDelayChange,
  onSynthFuzzDetuneChange,
  onSynthFuzzDriveChange,
  onSynthFuzzLevelChange,
  onSynthFuzzToggle,
  onOctaveTransposeChange,
  onOctaveMixChange,
  onOctaveToneChange,
  onOctaveMonoDetectorToggle,
  onOctaveToggle,
  onDoublerMixChange,
  onDoublerDelayChange,
  onDoublerDetuneChange,
  onDoublerToggle,
  onTremoloSpeedChange,
  onTremoloDepthChange,
  onTremoloLfoChange,
  onTremoloStereoPhaseToggle,
  onTremoloToggle,
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
            <div className={`tuner-note ${tunerBypassed || !tunerNote ? "is-muted" : ""}`.trim()}>
              {tunerBypassed ? "—" : (tunerNote || "—")}
            </div>
            <div className="tuner-cents-track">
              <div className="tuner-cents-center" />
              <div
                className="tuner-cents-needle"
                style={{
                  left: `${tunerBypassed
                    ? 50
                    : 50 + Math.max(-50, Math.min(50, tunerCents))}%`,
                }}
              />
            </div>
            <div className="tuner-cents-readout">
              {tunerBypassed
                ? "Off"
                : tunerNote
                  ? `${tunerCents > 0 ? "+" : ""}${tunerCents.toFixed(1)} ¢ · ${tunerFrequency.toFixed(1)} Hz`
                  : "Play a note"}
            </div>
            <div className="tuner-level-bar">
              <div
                className="tuner-level-fill"
                style={{
                  width: `${tunerBypassed
                    ? 0
                    : Math.max(0, Math.min(100, ((tunerLevel + 60) / 60) * 100))}%`,
                }}
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

        <article className={`fx-pedal-card fx-pedal-card-wide ${octaveBypassed ? "is-bypassed" : ""}`.trim()}>
          <p className="fx-pedal-title">Octave</p>
          <div className="knob-grid knob-grid-3">
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
            <Knob
              className="pedal-compact"
              label="Mix"
              min={0}
              max={1}
              step={0.01}
              value={octaveMix}
              onChange={onOctaveMixChange}
              accent="magenta"
              decimals={2}
            />
            <Knob
              className="pedal-compact"
              label="Tone"
              min={0}
              max={1}
              step={0.01}
              value={octaveTone}
              onChange={onOctaveToneChange}
              accent="magenta"
              decimals={2}
            />
          </div>
          <div style={{ display: "flex", gap: "0.5rem", alignItems: "center", justifyContent: "center" }}>
            <BypassButton
              label="Mono Det"
              enabled={octaveMonoDetector}
              onToggle={onOctaveMonoDetectorToggle}
            />
            <BypassButton
              label="Power"
              enabled={octaveBypassed}
              className="pedal-power"
              onToggle={onOctaveToggle}
            />
          </div>
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

        <article className={`fx-pedal-card ${overdriveBypassed ? "is-bypassed" : ""}`.trim()}>
          <p className="fx-pedal-title">Overdrive</p>
          <Knob
            className="pedal-compact"
            label="Drive"
            min={0}
            max={24}
            value={overdriveDrive}
            onChange={onOverdriveDriveChange}
            unit="dB"
            accent="green"
          />
          <div className="knob-grid knob-grid-2">
            <Knob
              className="pedal-compact"
              label="Tone"
              min={0}
              max={1}
              step={0.01}
              value={overdriveTone}
              onChange={onOverdriveToneChange}
              accent="green"
            />
            <Knob
              className="pedal-compact"
              label="Level"
              min={-24}
              max={12}
              value={overdriveLevel}
              onChange={onOverdriveLevelChange}
              unit="dB"
              accent="green"
            />
          </div>
          <BypassButton
            label="Power"
            enabled={overdriveBypassed}
            className="pedal-power"
            onToggle={onOverdriveToggle}
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

        <article className={`fx-pedal-card ${synthFuzzBypassed ? "is-bypassed" : ""}`.trim()}>
          <p className="fx-pedal-title">Synth Fuzz</p>
          <Knob
            className="pedal-compact"
            label="Mix"
            min={0}
            max={1}
            step={0.01}
            value={synthFuzzMix}
            onChange={onSynthFuzzMixChange}
            accent="red"
          />
          <div className="knob-grid knob-grid-2">
            <Knob
              className="pedal-compact"
              label="Delay"
              min={0}
              max={100}
              step={0.5}
              value={synthFuzzDelay}
              onChange={onSynthFuzzDelayChange}
              unit="ms"
              decimals={0}
              accent="red"
            />
            <Knob
              className="pedal-compact"
              label="Detune"
              min={-50}
              max={50}
              step={1}
              value={synthFuzzDetune}
              onChange={onSynthFuzzDetuneChange}
              unit="ct"
              decimals={0}
              accent="red"
            />
          </div>
          <div className="knob-grid knob-grid-2">
            <Knob
              className="pedal-compact"
              label="Drive"
              min={1}
              max={20}
              step={0.05}
              value={synthFuzzDrive}
              onChange={onSynthFuzzDriveChange}
              accent="red"
              decimals={2}
            />
            <Knob
              className="pedal-compact"
              label="Level"
              min={-24}
              max={12}
              value={synthFuzzLevel}
              onChange={onSynthFuzzLevelChange}
              unit="dB"
              accent="red"
            />
          </div>
          <BypassButton
            label="Power"
            enabled={synthFuzzBypassed}
            className="pedal-power"
            onToggle={onSynthFuzzToggle}
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

        <article className={`fx-pedal-card ${tremoloBypassed ? "is-bypassed" : ""}`.trim()}>
          <p className="fx-pedal-title">Tremolo</p>
          <div className="knob-grid knob-grid-2">
            <Knob
              className="pedal-compact"
              label="Speed"
              min={0.1}
              max={15}
              step={0.05}
              value={tremoloSpeed}
              onChange={onTremoloSpeedChange}
              unit="Hz"
              decimals={2}
              accent="yellow"
            />
            <Knob
              className="pedal-compact"
              label="Depth"
              min={0}
              max={1}
              step={0.01}
              value={tremoloDepth}
              onChange={onTremoloDepthChange}
              accent="yellow"
            />
          </div>
          <div className="pedal-mode-toggle">
            <div className="pedal-mode-option">
              <button
                type="button"
                className={`pedal-mode-circle ${tremoloLfoIndex === 0 ? "active" : ""}`.trim()}
                onClick={() => { if (tremoloLfoIndex !== 0) onTremoloLfoChange(0); }}
                aria-label="Sine"
              />
              <span className="pedal-mode-label">Sine</span>
            </div>
            <div className="pedal-mode-option">
              <button
                type="button"
                className={`pedal-mode-circle ${tremoloLfoIndex === 1 ? "active" : ""}`.trim()}
                onClick={() => { if (tremoloLfoIndex !== 1) onTremoloLfoChange(1); }}
                aria-label="Triangle"
              />
              <span className="pedal-mode-label">Tri</span>
            </div>
            <div className="pedal-mode-option">
              <button
                type="button"
                className={`pedal-mode-circle ${tremoloLfoIndex === 2 ? "active" : ""}`.trim()}
                onClick={() => { if (tremoloLfoIndex !== 2) onTremoloLfoChange(2); }}
                aria-label="Square"
              />
                <span className="pedal-mode-label">Square</span>
              </div>
            </div>
            <div style={{ display: "flex", gap: "0.5rem", alignItems: "center", justifyContent: "center" }}>
              <BypassButton
                label="Stereo"
                enabled={tremoloStereoPhase}
                onToggle={onTremoloStereoPhaseToggle}
              />
              <BypassButton
                label="Power"
                enabled={tremoloBypassed}
                className="pedal-power"
                onToggle={onTremoloToggle}
              />
            </div>
          </article>
      </div>
    </section>
  );
}
