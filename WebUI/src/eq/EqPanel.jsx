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
      <p className="section-title">Analyzer</p>
      <AnalyzerGraph
        responseCurve={responseCurve}
        leftSpectrum={analyzerEnabled ? leftSpectrum : []}
        rightSpectrum={analyzerEnabled ? rightSpectrum : []}
      />

      <div className="eq-main-grid">
        <Knob
          label="Low Cut"
          min={20}
          max={20000}
          value={lowCutFreq}
          onChange={onLowCutFreqChange}
          unit="Hz"
          step="1"
          accent="blue"
        />
        <Knob
          label="Peak Freq"
          min={20}
          max={20000}
          value={peakFreq}
          onChange={onPeakFreqChange}
          unit="Hz"
          step="1"
          accent="blue"
        />
        <Knob
          label="Peak Gain"
          min={-24}
          max={24}
          value={peakGain}
          onChange={onPeakGainChange}
          unit="dB"
          accent="blue"
        />
        <Knob
          label="Peak Q"
          min={0.1}
          max={10}
          value={peakQuality}
          onChange={onPeakQualityChange}
          unit="Q"
          step="0.05"
          accent="blue"
        />
        <Knob
          label="High Cut"
          min={20}
          max={20000}
          value={highCutFreq}
          onChange={onHighCutFreqChange}
          unit="Hz"
          step="1"
          accent="blue"
        />
      </div>

      <div className="eq-extra-grid">
        <SlopeKnob label="Low Slope" value={lowCutSlope} onChange={onLowCutSlopeChange} />
        <SlopeKnob label="High Slope" value={highCutSlope} onChange={onHighCutSlopeChange} />

        <div className="bypass-row">
          <BypassButton label="Low Cut" enabled={lowCutBypassed} onToggle={onLowCutBypassToggle} />
          <BypassButton label="Peak" enabled={peakBypassed} onToggle={onPeakBypassToggle} />
          <BypassButton label="High Cut" enabled={highCutBypassed} onToggle={onHighCutBypassToggle} />
        </div>

        <BypassButton label="Analyzer" enabled={!analyzerEnabled} onToggle={onAnalyzerToggle} />
      </div>
    </section>
  );
}
