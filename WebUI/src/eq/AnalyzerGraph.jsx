import { useMemo } from "react";

function buildPath(points, minValue, maxValue) {
  if (!Array.isArray(points) || points.length === 0) return "";

  const clampValue = (value, fallback) => {
    const numericValue = Number.isFinite(value) ? value : fallback;
    return Math.min(maxValue, Math.max(minValue, numericValue));
  };

  const map = (input) => {
    const normalized = (input - minValue) / (maxValue - minValue);
    return (1 - normalized) * 100;
  };

  let lastValid = minValue;
  return points
    .map((value, index) => {
      const x = points.length === 1 ? 0 : (index / (points.length - 1)) * 100;
      const clamped = clampValue(value, lastValid);
      lastValid = clamped;
      const y = map(clamped);
      return `${index === 0 ? "M" : "L"}${x.toFixed(2)},${y.toFixed(2)}`;
    })
    .join(" ");
}

function formatDb(value) {
  const rounded = Math.round(value);
  if (rounded === 0) return "0 dB";
  return `${rounded > 0 ? "+" : ""}${rounded} dB`;
}

function buildYAxisLabels(minValue, maxValue, gridLines) {
  return gridLines.map((y) => {
    const normalizedFromTop = (100 - y) / 100;
    const value = normalizedFromTop * (maxValue - minValue) + minValue;
    return { y, label: formatDb(value) };
  });
}

export default function AnalyzerGraph({ responseCurve, leftSpectrum, rightSpectrum }) {
  const responsePath = useMemo(() => buildPath(responseCurve, -24, 24), [responseCurve]);
  const leftSpectrumPath = useMemo(() => buildPath(leftSpectrum, -48, 0), [leftSpectrum]);
  const rightSpectrumPath = useMemo(() => buildPath(rightSpectrum, -48, 0), [rightSpectrum]);

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

  const gridLines = [10, 30, 50, 70, 90];
  const spectrumYLabels = useMemo(() => buildYAxisLabels(-48, 0, gridLines), []);
  const responseYLabels = useMemo(() => buildYAxisLabels(-24, 24, gridLines), []);

  return (
    <div className="analyzer-panel">
      <div className="analyzer-graph-wrap">
        <div className="analyzer-y-axis analyzer-y-axis-left" aria-label="Spectrum level in dB">
          {spectrumYLabels.map(({ y, label }) => (
            <span key={label} className="analyzer-y-label" style={{ top: `${y}%` }}>
              {label}
            </span>
          ))}
        </div>

        <svg className="analyzer-svg" viewBox="0 0 100 100" preserveAspectRatio="none" aria-label="EQ FFT Analyzer">
          <g className="analyzer-grid">
            {gridLines.map((y) => (
              <line key={y} x1="0" y1={y} x2="100" y2={y} />
            ))}
          </g>
          {rightSpectrumPath ? <path className="analyzer-path analyzer-path-right" d={rightSpectrumPath} /> : null}
          {leftSpectrumPath ? <path className="analyzer-path analyzer-path-left" d={leftSpectrumPath} /> : null}
          {responsePath ? <path className="analyzer-path analyzer-path-response" d={responsePath} /> : null}
        </svg>

        <div className="analyzer-y-axis analyzer-y-axis-right" aria-label="EQ response in dB">
          {responseYLabels.map(({ y, label }) => (
            <span key={label} className="analyzer-y-label" style={{ top: `${y}%` }}>
              {label}
            </span>
          ))}
        </div>
      </div>

      <div className="analyzer-freq-axis" aria-label="Analyzer Frequency Axis">
        {frequencyTicks.map((tick, index) => {
          const edgeClass = index === 0 ? "is-start" : index === frequencyTicks.length - 1 ? "is-end" : "";

          return (
            <span key={tick.frequency} className={`analyzer-freq-label ${edgeClass}`.trim()} style={{ left: `${tick.x}%` }}>
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
