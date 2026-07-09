import { BypassButton, Knob } from "../components/controls";
import AnalyzerGraph from "./AnalyzerGraph";

const BAND_LABELS = ["31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"];

export default function EqPanel({
  responseCurve,
  leftSpectrum,
  rightSpectrum,
  analyzerEnabled,
  eqBands,
  eqBypassed,
  onEqBandChange,
  onEqBypassToggle,
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

      <article className={`eq-pedal pedal-accent-tan ${eqBypassed ? "is-bypassed" : ""}`.trim()}>
        <p className="fx-pedal-title">10-Band EQ</p>
        <div className="eq-band-grid">
          {BAND_LABELS.map((label, i) => (
            <Knob
              key={i}
              className="eq-band-knob"
              label={label}
              min={-12}
              max={12}
              step={0.1}
              value={eqBands[i] ?? 0}
              onChange={(next) => onEqBandChange(i, next)}
              unit="dB"
              accent="tan"
            />
          ))}
        </div>
        <BypassButton
          label="Power"
          enabled={eqBypassed}
          className="pedal-power"
          onToggle={onEqBypassToggle}
        />
      </article>
    </section>
  );
}
