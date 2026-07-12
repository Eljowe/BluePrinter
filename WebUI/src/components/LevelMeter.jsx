export function LevelMeter({ level = 0, peak = 0 }) {
  const safeLevel = Math.min(1, Math.max(0, level));
  const safePeak = Math.min(1, Math.max(0, peak));

  return (
    <div className="level-meter" role="meter" aria-label="Input level" aria-valuemin={0} aria-valuemax={1} aria-valuenow={safeLevel}>
      <div className="level-meter-track">
        <div
          className="level-meter-fill"
          style={{ width: `${safeLevel * 100}%` }}
        />
        <div
          className="level-meter-peak"
          style={{ left: `${safePeak * 100}%`, opacity: safePeak > 0.01 ? 1 : 0 }}
        />
        <div className="level-meter-tick" style={{ left: "60%" }} />
        <div className="level-meter-tick" style={{ left: "85%" }} />
      </div>
    </div>
  );
}
