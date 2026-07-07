import { useEffect, useMemo, useRef, useState } from "react";

const PARAM_IDS = {
  input: "Input Gain",
  compressorAmount: "Compressor Amount",
  drive: "Distortion Drive",
  output: "Output Gain",
  lowCutFreq: "LowCut Freq",
  highCutFreq: "HighCut Freq",
  peakFreq: "Peak Freq",
  peakGain: "Peak Gain",
  peakQuality: "Peak Quality",
  lowCutSlope: "LowCut Slope",
  highCutSlope: "HighCut Slope",
  lowCutBypassed: "LowCut Bypassed",
  peakBypassed: "Peak Bypassed",
  highCutBypassed: "HighCut Bypassed",
  driveBypassed: "Distortion Bypassed",
  compressorBypassed: "Compressor Bypassed",
};

const FRONTEND_EVENT = "frontendSetParameter";
const BACKEND_EVENT = "backendParameters";

function getBackend() {
  return window.__JUCE__?.backend;
}

function emitParameterChange(id, value) {
  const backend = getBackend();
  if (!backend?.emitEvent) return;
  backend.emitEvent(FRONTEND_EVENT, { id, value });
}

function readInitialParametersFromJuce() {
  const first = window.__JUCE__?.initialisationData?.parameters?.[0];
  if (!first) return null;

  return {
    inputGain: Number(first.inputGain ?? 0),
    compressorAmount: Number(first.compressorAmount ?? 0),
    drive: Number(first.drive ?? 6),
    outputGain: Number(first.outputGain ?? 0),
    lowCutFreq: Number(first.lowCutFreq ?? 20),
    highCutFreq: Number(first.highCutFreq ?? 20000),
    peakFreq: Number(first.peakFreq ?? 750),
    peakGain: Number(first.peakGain ?? 0),
    peakQuality: Number(first.peakQuality ?? 1),
    lowCutSlope: Number(first.lowCutSlope ?? 0),
    highCutSlope: Number(first.highCutSlope ?? 0),
    lowCutBypassed: Boolean(first.lowCutBypassed),
    peakBypassed: Boolean(first.peakBypassed),
    highCutBypassed: Boolean(first.highCutBypassed),
    driveBypassed: Boolean(first.driveBypassed),
    compressorBypassed: Boolean(first.compressorBypassed),
    responseCurve: Array.isArray(first.responseCurve) ? first.responseCurve.map(Number) : [],
    leftSpectrum: Array.isArray(first.leftSpectrum) ? first.leftSpectrum.map(Number) : [],
    rightSpectrum: Array.isArray(first.rightSpectrum) ? first.rightSpectrum.map(Number) : [],
    inputPeak: Number(first.inputPeak ?? 0),
    outputPeak: Number(first.outputPeak ?? 0),
    inputClipping: Boolean(first.inputClipping),
    outputClipping: Boolean(first.outputClipping),
  };
}

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function snapToStep(value, min, step) {
  const stepValue = Number(step);
  if (!Number.isFinite(stepValue) || stepValue <= 0) return value;

  const snapped = Math.round((value - min) / stepValue) * stepValue + min;
  const precision = (String(step).split(".")[1] || "").length;
  return Number(snapped.toFixed(precision));
}

function buildPath(points, minValue, maxValue) {
  if (!Array.isArray(points) || points.length === 0) return "";

  const clampValue = (value, fallback) => {
    const numericValue = Number.isFinite(value) ? value : fallback;
    return Math.min(maxValue, Math.max(minValue, numericValue));
  };

  let lastValid = minValue;
  return points
    .map((value, index) => {
      const x = points.length === 1 ? 0 : (index / (points.length - 1)) * 100;
      const clamped = clampValue(value, lastValid);
      lastValid = clamped;
      const normalizedY = (clamped - minValue) / (maxValue - minValue);
      const y = (1 - normalizedY) * 100;
      return `${index === 0 ? "M" : "L"}${x.toFixed(2)},${y.toFixed(2)}`;
    })
    .join(" ");
}

function Knob({ label, min, max, value, onChange, unit, step = "0.1", className = "", accent = "orange" }) {
  const normalized = (value - min) / (max - min);
  const rotation = -140 + normalized * 280;
  const dragState = useRef(null);
  const range = max - min;

  const updateValue = (next) => {
    const clamped = clamp(next, min, max);
    onChange(snapToStep(clamped, min, step));
  };

  const beginDrag = (event) => {
    event.preventDefault();
    dragState.current = {
      pointerId: event.pointerId,
      startY: event.clientY,
      startX: event.clientX,
      startValue: value,
    };
    event.currentTarget.setPointerCapture?.(event.pointerId);
  };

  const continueDrag = (event) => {
    if (!dragState.current || dragState.current.pointerId !== event.pointerId) return;
    const deltaY = dragState.current.startY - event.clientY;
    const deltaX = event.clientX - dragState.current.startX;
    const delta = deltaY + deltaX * 0.5;
    const sensitivity = range / 180;
    updateValue(dragState.current.startValue + delta * sensitivity);
  };

  const endDrag = (event) => {
    if (!dragState.current || dragState.current.pointerId !== event.pointerId) return;
    event.currentTarget.releasePointerCapture?.(event.pointerId);
    dragState.current = null;
  };

  const handleWheel = (event) => {
    event.preventDefault();
    const direction = event.deltaY < 0 ? 1 : -1;
    const fineStep = Number(step) || range / 100;
    updateValue(value + direction * fineStep);
  };

  const handleKeyDown = (event) => {
    const fineStep = Number(step) || range / 100;
    const coarseStep = fineStep * 10;

    switch (event.key) {
      case "ArrowUp":
      case "ArrowRight":
        event.preventDefault();
        updateValue(value + fineStep);
        break;
      case "ArrowDown":
      case "ArrowLeft":
        event.preventDefault();
        updateValue(value - fineStep);
        break;
      case "PageUp":
        event.preventDefault();
        updateValue(value + coarseStep);
        break;
      case "PageDown":
        event.preventDefault();
        updateValue(value - coarseStep);
        break;
      case "Home":
        event.preventDefault();
        updateValue(min);
        break;
      case "End":
        event.preventDefault();
        updateValue(max);
        break;
      default:
        break;
    }
  };

  return (
    <div className={`knob-wrap knob-${accent} ${className}`.trim()}>
      <div
        className="knob-shell"
        role="slider"
        tabIndex={0}
        aria-label={label}
        aria-valuemin={min}
        aria-valuemax={max}
        aria-valuenow={value}
        onPointerDown={beginDrag}
        onPointerMove={continueDrag}
        onPointerUp={endDrag}
        onPointerCancel={endDrag}
        onWheel={handleWheel}
        onKeyDown={handleKeyDown}
      >
        <div className="knob-face" style={{ transform: `rotate(${rotation}deg)` }}>
          <div className="knob-pointer" />
        </div>
      </div>
      <div className="knob-label">{label}</div>
      <div className="knob-value">
        {value.toFixed(1)} {unit}
      </div>
    </div>
  );
}

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

function BypassButton({ label, enabled, onToggle, className = "" }) {
  return (
    <button className={`bypass-button ${enabled ? "active" : ""} ${className}`.trim()} type="button" onClick={onToggle}>
      <span>{label}</span>
      <strong>{enabled ? "Off" : "On"}</strong>
    </button>
  );
}

function Meter({ label, level, clipping }) {
  const normalizedLevel = clamp(level, 0, 1.25);
  const fillHeight = `${Math.min(100, normalizedLevel * 100)}%`;

  return (
    <div className="meter-card">
      <div className="meter-header">
        <span className="meter-label">{label}</span>
        <span className={`clip-pill ${clipping ? "active" : ""}`}>Clip</span>
      </div>
      <div className="meter-body">
        <div className="meter-track">
          <div className="meter-fill" style={{ height: fillHeight }} />
        </div>
        <div className="meter-scale">
          <span>0</span>
          <span>-6</span>
          <span>-12</span>
          <span>-24</span>
        </div>
      </div>
    </div>
  );
}

function MiniPeak({ label, level, clipping, className = "" }) {
  const height = `${Math.min(100, Math.max(0, level * 100))}%`;

  return (
    <div className={`mini-peak ${className}`.trim()}>
      <div className="mini-peak-head">
        <span>{label}</span>
        <span className={`mini-clip ${clipping ? "active" : ""}`}>{clipping ? "CLIP" : "OK"}</span>
      </div>
      <div className="mini-peak-track">
        <div className="mini-peak-fill" style={{ height }} />
      </div>
    </div>
  );
}

function AnalyzerGraph({ responseCurve, leftSpectrum, rightSpectrum }) {
  const responsePath = useMemo(() => buildPath(responseCurve, -24, 24), [responseCurve]);
  const leftSpectrumPath = useMemo(() => buildPath(leftSpectrum, -120, 0), [leftSpectrum]);
  const rightSpectrumPath = useMemo(() => buildPath(rightSpectrum, -120, 0), [rightSpectrum]);
  const frequencyTicks = useMemo(() => {
    const tickFrequencies = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000];
    const minLog = Math.log10(20);
    const maxLog = Math.log10(20000);

    return tickFrequencies.map((frequency) => {
      const x = ((Math.log10(frequency) - minLog) / (maxLog - minLog)) * 100;
      const label = frequency >= 1000 ? `${frequency / 1000}k` : `${frequency}`;
      return { frequency, x, label };
    });
  }, []);

  return (
    <div className="analyzer-panel">
      <svg className="analyzer-svg" viewBox="0 0 100 100" preserveAspectRatio="none" aria-label="EQ FFT Analyzer">
        <g className="analyzer-grid">
          <line x1="0" y1="10" x2="100" y2="10" />
          <line x1="0" y1="30" x2="100" y2="30" />
          <line x1="0" y1="50" x2="100" y2="50" />
          <line x1="0" y1="70" x2="100" y2="70" />
          <line x1="0" y1="90" x2="100" y2="90" />
        </g>
        {rightSpectrumPath ? <path className="analyzer-path analyzer-path-right" d={rightSpectrumPath} /> : null}
        {leftSpectrumPath ? <path className="analyzer-path analyzer-path-left" d={leftSpectrumPath} /> : null}
        {responsePath ? <path className="analyzer-path analyzer-path-response" d={responsePath} /> : null}
      </svg>
      <div className="analyzer-freq-axis" aria-label="Analyzer Frequency Axis">
        {frequencyTicks.map((tick, index) => {
          const edgeClass = index === 0 ? "is-start" : index === frequencyTicks.length - 1 ? "is-end" : "";

          return (
            <span
              key={tick.frequency}
              className={`analyzer-freq-label ${edgeClass}`.trim()}
              style={{ left: `${tick.x}%` }}
            >
              {tick.label}
            </span>
          );
        })}
      </div>
      <div className="analyzer-legend">
        <span className="legend-item legend-left">L FFT</span>
        <span className="legend-item legend-right">R FFT</span>
        <span className="legend-item legend-response">EQ Curve</span>
      </div>
    </div>
  );
}

export default function App() {
  const initial = useMemo(() => readInitialParametersFromJuce(), []);

  const [activeTab, setActiveTab] = useState("fx");
  const [inGain, setInGain] = useState(initial?.inputGain ?? 0);
  const [drive, setDrive] = useState(initial?.drive ?? 6);
  const [compressorAmount, setCompressorAmount] = useState(initial?.compressorAmount ?? 0);
  const [outGain, setOutGain] = useState(initial?.outputGain ?? 0);
  const [lowCutFreq, setLowCutFreq] = useState(initial?.lowCutFreq ?? 20);
  const [highCutFreq, setHighCutFreq] = useState(initial?.highCutFreq ?? 20000);
  const [peakFreq, setPeakFreq] = useState(initial?.peakFreq ?? 750);
  const [peakGain, setPeakGain] = useState(initial?.peakGain ?? 0);
  const [peakQuality, setPeakQuality] = useState(initial?.peakQuality ?? 1);
  const [lowCutSlope, setLowCutSlope] = useState(initial?.lowCutSlope ?? 0);
  const [highCutSlope, setHighCutSlope] = useState(initial?.highCutSlope ?? 0);
  const [lowCutBypassed, setLowCutBypassed] = useState(initial?.lowCutBypassed ?? false);
  const [peakBypassed, setPeakBypassed] = useState(initial?.peakBypassed ?? false);
  const [highCutBypassed, setHighCutBypassed] = useState(initial?.highCutBypassed ?? false);
  const [driveBypassed, setDriveBypassed] = useState(initial?.driveBypassed ?? false);
  const [compressorBypassed, setCompressorBypassed] = useState(initial?.compressorBypassed ?? false);
  const [responseCurve, setResponseCurve] = useState(initial?.responseCurve ?? []);
  const [leftSpectrum, setLeftSpectrum] = useState(initial?.leftSpectrum ?? []);
  const [rightSpectrum, setRightSpectrum] = useState(initial?.rightSpectrum ?? []);
  const [inputPeak, setInputPeak] = useState(initial?.inputPeak ?? 0);
  const [outputPeak, setOutputPeak] = useState(initial?.outputPeak ?? 0);
  const [inputClipping, setInputClipping] = useState(initial?.inputClipping ?? false);
  const [outputClipping, setOutputClipping] = useState(initial?.outputClipping ?? false);

  useEffect(() => {
    const backend = getBackend();
    if (!backend?.addEventListener) return undefined;

    const token = backend.addEventListener(BACKEND_EVENT, (payload) => {
      if (typeof payload !== "object" || payload == null) return;
      if (payload.inputGain !== undefined) setInGain(Number(payload.inputGain));
      if (payload.compressorAmount !== undefined) setCompressorAmount(Number(payload.compressorAmount));
      if (payload.drive !== undefined) setDrive(Number(payload.drive));
      if (payload.outputGain !== undefined) setOutGain(Number(payload.outputGain));
      if (payload.lowCutFreq !== undefined) setLowCutFreq(Number(payload.lowCutFreq));
      if (payload.highCutFreq !== undefined) setHighCutFreq(Number(payload.highCutFreq));
      if (payload.peakFreq !== undefined) setPeakFreq(Number(payload.peakFreq));
      if (payload.peakGain !== undefined) setPeakGain(Number(payload.peakGain));
      if (payload.peakQuality !== undefined) setPeakQuality(Number(payload.peakQuality));
      if (payload.lowCutSlope !== undefined) setLowCutSlope(Number(payload.lowCutSlope));
      if (payload.highCutSlope !== undefined) setHighCutSlope(Number(payload.highCutSlope));
      if (payload.lowCutBypassed !== undefined) setLowCutBypassed(Boolean(payload.lowCutBypassed));
      if (payload.peakBypassed !== undefined) setPeakBypassed(Boolean(payload.peakBypassed));
      if (payload.highCutBypassed !== undefined) setHighCutBypassed(Boolean(payload.highCutBypassed));
      if (payload.driveBypassed !== undefined) setDriveBypassed(Boolean(payload.driveBypassed));
      if (payload.compressorBypassed !== undefined) setCompressorBypassed(Boolean(payload.compressorBypassed));
      if (Array.isArray(payload.responseCurve)) setResponseCurve(payload.responseCurve.map(Number));
      if (Array.isArray(payload.leftSpectrum)) setLeftSpectrum(payload.leftSpectrum.map(Number));
      if (Array.isArray(payload.rightSpectrum)) setRightSpectrum(payload.rightSpectrum.map(Number));
      if (payload.inputPeak !== undefined) setInputPeak(Number(payload.inputPeak));
      if (payload.outputPeak !== undefined) setOutputPeak(Number(payload.outputPeak));
      if (payload.inputClipping !== undefined) setInputClipping(Boolean(payload.inputClipping));
      if (payload.outputClipping !== undefined) setOutputClipping(Boolean(payload.outputClipping));
    });

    return () => {
      if (backend.removeEventListener && token !== undefined) {
        backend.removeEventListener(token);
      }
    };
  }, []);

  return (
    <main className="app">
      <header className="chrome-header">
        <div className="brand-block">
          <span className="brand-mark">A</span>
          <div>
            <p className="brand-name">IRONFORGE</p>
            <p className="brand-version">v2.4.1</p>
          </div>
        </div>

        <nav className="tab-row" aria-label="Editor sections">
          <button
            className={`tab-button ${activeTab === "fx" ? "active" : ""}`}
            onClick={() => setActiveTab("fx")}
            type="button"
          >
            FX
          </button>
          <button
            className={`tab-button ${activeTab === "eq" ? "active" : ""}`}
            onClick={() => setActiveTab("eq")}
            type="button"
          >
            EQ
          </button>
        </nav>

        <div className="io-dock">
          <div className="io-row">
            <MiniPeak className="io-meter" label="Input" level={inputPeak} clipping={inputClipping} />
            <Knob
              className="io-compact"
              label="Input"
              min={-24}
              max={24}
              value={inGain}
              onChange={(next) => {
                setInGain(next);
                emitParameterChange(PARAM_IDS.input, next);
              }}
              unit="dB"
              accent="orange"
            />
            <Knob
              className="io-compact"
              label="Output"
              min={-24}
              max={24}
              value={outGain}
              onChange={(next) => {
                setOutGain(next);
                emitParameterChange(PARAM_IDS.output, next);
              }}
              unit="dB"
              accent="yellow"
            />
            <MiniPeak className="io-meter" label="Output" level={outputPeak} clipping={outputClipping} />
          </div>
        </div>
      </header>

      {activeTab === "fx" ? (
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
                onChange={(next) => {
                  setDrive(next);
                  emitParameterChange(PARAM_IDS.drive, next);
                }}
                unit="dB"
                accent="orange"
              />
              <BypassButton
                label="Power"
                enabled={driveBypassed}
                className="pedal-power"
                onToggle={() => {
                  const next = !driveBypassed;
                  setDriveBypassed(next);
                  emitParameterChange(PARAM_IDS.driveBypassed, next ? 1 : 0);
                }}
              />
            </article>

            <article
              className={`fx-pedal-card fx-pedal-card-secondary ${compressorBypassed ? "is-bypassed" : ""}`.trim()}
            >
              <p className="fx-pedal-title">Compressor</p>
              <Knob
                className="pedal-compact"
                label="Comp"
                min={0}
                max={24}
                value={compressorAmount}
                onChange={(next) => {
                  setCompressorAmount(next);
                  emitParameterChange(PARAM_IDS.compressorAmount, next);
                }}
                unit="dB"
                accent="blue"
              />
              <BypassButton
                label="Power"
                enabled={compressorBypassed}
                className="pedal-power"
                onToggle={() => {
                  const next = !compressorBypassed;
                  setCompressorBypassed(next);
                  emitParameterChange(PARAM_IDS.compressorBypassed, next ? 1 : 0);
                }}
              />
            </article>
          </div>
        </section>
      ) : (
        <section className="panel-shell eq-shell">
          <p className="section-title">Analyzer</p>
          <AnalyzerGraph responseCurve={responseCurve} leftSpectrum={leftSpectrum} rightSpectrum={rightSpectrum} />

          <div className="eq-main-grid">
            <Knob
              label="Low Cut"
              min={20}
              max={20000}
              value={lowCutFreq}
              onChange={(next) => {
                setLowCutFreq(next);
                emitParameterChange(PARAM_IDS.lowCutFreq, next);
              }}
              unit="Hz"
              step="1"
              accent="blue"
            />
            <Knob
              label="Peak Freq"
              min={20}
              max={20000}
              value={peakFreq}
              onChange={(next) => {
                setPeakFreq(next);
                emitParameterChange(PARAM_IDS.peakFreq, next);
              }}
              unit="Hz"
              step="1"
              accent="blue"
            />
            <Knob
              label="Peak Gain"
              min={-24}
              max={24}
              value={peakGain}
              onChange={(next) => {
                setPeakGain(next);
                emitParameterChange(PARAM_IDS.peakGain, next);
              }}
              unit="dB"
              accent="blue"
            />
            <Knob
              label="Peak Q"
              min={0.1}
              max={10}
              value={peakQuality}
              onChange={(next) => {
                setPeakQuality(next);
                emitParameterChange(PARAM_IDS.peakQuality, next);
              }}
              unit="Q"
              step="0.05"
              accent="blue"
            />
            <Knob
              label="High Cut"
              min={20}
              max={20000}
              value={highCutFreq}
              onChange={(next) => {
                setHighCutFreq(next);
                emitParameterChange(PARAM_IDS.highCutFreq, next);
              }}
              unit="Hz"
              step="1"
              accent="blue"
            />
          </div>

          <div className="eq-extra-grid">
            <SlopeKnob
              label="Low Slope"
              value={lowCutSlope}
              onChange={(next) => {
                setLowCutSlope(next);
                emitParameterChange(PARAM_IDS.lowCutSlope, next);
              }}
            />
            <SlopeKnob
              label="High Slope"
              value={highCutSlope}
              onChange={(next) => {
                setHighCutSlope(next);
                emitParameterChange(PARAM_IDS.highCutSlope, next);
              }}
            />

            <div className="bypass-row">
              <BypassButton
                label="Low Cut"
                enabled={lowCutBypassed}
                onToggle={() => {
                  const next = !lowCutBypassed;
                  setLowCutBypassed(next);
                  emitParameterChange(PARAM_IDS.lowCutBypassed, next ? 1 : 0);
                }}
              />
              <BypassButton
                label="Peak"
                enabled={peakBypassed}
                onToggle={() => {
                  const next = !peakBypassed;
                  setPeakBypassed(next);
                  emitParameterChange(PARAM_IDS.peakBypassed, next ? 1 : 0);
                }}
              />
              <BypassButton
                label="High Cut"
                enabled={highCutBypassed}
                onToggle={() => {
                  const next = !highCutBypassed;
                  setHighCutBypassed(next);
                  emitParameterChange(PARAM_IDS.highCutBypassed, next ? 1 : 0);
                }}
              />
            </div>
          </div>
        </section>
      )}

      <footer className="status-bar">
        <span>Bridge active</span>
        <span>JUCE APVTS live sync</span>
      </footer>
    </main>
  );
}
