export function Waveform({ peaks = [], width = 360, height = 56 }) {
  if (!peaks || peaks.length === 0) {
    return (
      <svg width={width} height={height} className="waveform waveform-empty" aria-label="Waveform">
        <line x1={0} y1={height / 2} x2={width} y2={height / 2} stroke="#333" strokeWidth={1} />
      </svg>
    );
  }

  const middle = height / 2;
  const stepX = width / peaks.length;
  const points = peaks
    .map((p, i) => {
      const x = i * stepX + stepX / 2;
      const v = Math.min(1, Math.max(0, p));
      const half = v * middle;
      return `${x},${middle - half} ${x},${middle + half}`;
    })
    .join(" ");

  return (
    <svg width={width} height={height} className="waveform" aria-label="Waveform">
      <polyline points={points} stroke="#4a9eff" strokeWidth={1} fill="none" strokeLinecap="round" />
      <line x1={0} y1={middle} x2={width} y2={middle} stroke="#222" strokeWidth={0.5} />
    </svg>
  );
}
