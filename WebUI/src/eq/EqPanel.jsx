import { BypassButton, Knob } from "../components/controls";
import AnalyzerGraph from "./AnalyzerGraph";

function SlopeKnob({ label, value, onChange }) {
  const snapped = Math.round(value);
  const slopeLabel = `${12 + snapped * 12} dB/Oct`;

  return (
    <div className="eq-extra-item">
      <Knob
        label={label}
        min={0}
        max={3}
        step="1"
        value={snapped}
        onChange={(next) => onChange(Math.round(next))}
        unit=""
        accent="blue"
      />
      <div className="eq-extra-value">{slopeLabel}</div>
    </div>
  );
}

export default function EqPanel({
  responseCurve,
  leftSpectrum,
  rightSpectrum,
  analyzerEnabled,
  lowCutFreq,
  peakFreq,
  peakGain,
  peakQuality,
  highCutFreq,
  lowCutSlope,
  highCutSlope,
  lowCutBypassed,
  peakBypassed,
  highCutBypassed,
  onLowCutFreqChange,
  onPeakFreqChange,
  onPeakGainChange,
  onPeakQualityChange,
  onHighCutFreqChange,
  onLowCutSlopeChange,
  onHighCutSlopeChange,
  onLowCutBypassToggle,
  onPeakBypassToggle,
  onHighCutBypassToggle,
  onAnalyzerToggle,
}) {
  return (
    <section className="panel-shell eq-shell">
      <div className="eq-header">
        <p className="section-title">Analyzer</p>
        <div className="eq-analyzer-toggle">
          <BypassButton
            label="Analyzer"
            enabled={!analyzerEnabled}
            onToggle={onAnalyzerToggle}
            className="pedal-power"
          />
          <span className="eq-analyzer-label">{analyzerEnabled ? "On" : "Off"}</span>
        </div>
      </div>
      <AnalyzerGraph
        responseCurve={responseCurve}
        leftSpectrum={analyzerEnabled ? leftSpectrum : []}
        rightSpectrum={analyzerEnabled ? rightSpectrum : []}
      />

      <div className="fx-pedal-grid eq-bands-grid">
        <article className={`fx-pedal-card ${lowCutBypassed ? "is-bypassed" : ""}`.trim()}>
          <p className="fx-pedal-title">Low Cut</p>
          <div className="knob-grid knob-grid-2">
            <Knob
              label="Freq"
              min={20}
              max={20000}
              value={lowCutFreq}
              onChange={onLowCutFreqChange}
              unit="Hz"
              step="1"
              accent="blue"
            />
            <SlopeKnob label="Slope" value={lowCutSlope} onChange={onLowCutSlopeChange} />
          </div>
          <BypassButton
            label="Low Cut"
            enabled={lowCutBypassed}
            onToggle={onLowCutBypassToggle}
            className="pedal-power"
          />
        </article>

        <article className={`fx-pedal-card ${peakBypassed ? "is-bypassed" : ""}`.trim()}>
          <p className="fx-pedal-title">Peak</p>
          <Knob
            label="Freq"
            min={20}
            max={20000}
            value={peakFreq}
            onChange={onPeakFreqChange}
            unit="Hz"
            step="1"
            accent="blue"
          />
          <div className="knob-grid knob-grid-2">
            <Knob
              label="Gain"
              min={-24}
              max={24}
              value={peakGain}
              onChange={onPeakGainChange}
              unit="dB"
              accent="blue"
            />
            <Knob
              label="Q"
              min={0.1}
              max={10}
              value={peakQuality}
              onChange={onPeakQualityChange}
              unit="Q"
              step="0.05"
              accent="blue"
            />
          </div>
          <BypassButton
            label="Peak"
            enabled={peakBypassed}
            onToggle={onPeakBypassToggle}
            className="pedal-power"
          />
        </article>

        <article className={`fx-pedal-card ${highCutBypassed ? "is-bypassed" : ""}`.trim()}>
          <p className="fx-pedal-title">High Cut</p>
          <div className="knob-grid knob-grid-2">
            <Knob
              label="Freq"
              min={20}
              max={20000}
              value={highCutFreq}
              onChange={onHighCutFreqChange}
              unit="Hz"
              step="1"
              accent="blue"
            />
            <SlopeKnob label="Slope" value={highCutSlope} onChange={onHighCutSlopeChange} />
          </div>
          <BypassButton
            label="High Cut"
            enabled={highCutBypassed}
            onToggle={onHighCutBypassToggle}
            className="pedal-power"
          />
        </article>
      </div>
    </section>
  );
}
