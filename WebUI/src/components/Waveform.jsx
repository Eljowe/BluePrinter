export function Waveform({ peaks = [], width = 360, height = 56 }) {
  const viewBox = `0 0 ${width} ${height}`;

  if (!peaks || peaks.length === 0) {
    return (
      <svg viewBox={viewBox} preserveAspectRatio="none" className="waveform waveform-empty" aria-label="Waveform">
        <line className="waveform-baseline" x1={0} y1={height / 2} x2={width} y2={height / 2} strokeWidth={1} vectorEffect="non-scaling-stroke" />
      </svg>
    );
  }

  const middle = height / 2;
  const stepX = peaks.length > 1 ? width / (peaks.length - 1) : 0;
  const points = peaks
    .map((p, i) => {
      const x = i * stepX;
      const v = Math.min(1, Math.max(0, p));
      const half = v * middle;
      return `${x},${middle - half} ${x},${middle + half}`;
    })
    .join(" ");

  return (
    <svg viewBox={viewBox} preserveAspectRatio="none" className="waveform" aria-label="Waveform">
      <line className="waveform-baseline" x1={0} y1={middle} x2={width} y2={middle} strokeWidth={0.5} vectorEffect="non-scaling-stroke" />
      <polyline className="waveform-trace" points={points} strokeWidth={1} fill="none" strokeLinecap="round" vectorEffect="non-scaling-stroke" />
    </svg>
  );
}
