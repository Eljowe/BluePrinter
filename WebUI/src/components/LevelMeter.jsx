// The meter spans -60..0 dBFS. The stored values are linear 0..1, so we
// map them through 20*log10 on the way to the pixels; 0 dBFS sits at the
// far right and anything at or above it latches the clip indicator.
const MIN_DB = -60;
const HEADROOM_DB = -6;

function toDb(value) {
  const v = Math.min(1, Math.max(0, Number(value) || 0));
  if (v <= 0) return -Infinity;
  return 20 * Math.log10(v);
}

function dbToPct(db) {
  if (!Number.isFinite(db)) return 0;
  return Math.min(100, Math.max(0, ((db - MIN_DB) / -MIN_DB) * 100));
}

export function LevelMeter({
  level = 0,
  peak = 0,
  label = "Level",
  clipped = false,
  onResetClip,
  className = "",
}) {
  const safeLevel = Math.min(1, Math.max(0, Number(level) || 0));
  const safePeak = Math.min(1, Math.max(0, Number(peak) || 0));
  const levelPct = dbToPct(toDb(safeLevel));
  const peakPct = dbToPct(toDb(safePeak));
  // The fill reads red only while the live signal is in the headroom zone —
  // the latched `clipped` flag colours the clip LED, not the bar, so a past
  // peak doesn't keep a quiet signal looking hot forever.
  const hot = toDb(safeLevel) >= HEADROOM_DB;

  return (
    <div
      className={`level-meter ${clipped ? "is-clipped" : ""} ${hot ? "is-hot" : ""} ${className}`.trim()}
      role="meter"
      aria-label={label}
      aria-valuemin={MIN_DB}
      aria-valuemax={0}
      aria-valuenow={Math.round(toDb(safeLevel))}
    >
      <div className="level-meter-track">
        <div className="level-meter-headroom" style={{ left: `${dbToPct(HEADROOM_DB)}%` }} />
        <div className="level-meter-fill" style={{ width: `${levelPct}%` }} />
        <div
          className="level-meter-peak"
          style={{ left: `${peakPct}%`, opacity: safePeak > 0.001 ? 1 : 0 }}
        />
        <div className="level-meter-tick" style={{ left: `${dbToPct(HEADROOM_DB)}%` }} />
        <div className="level-meter-tick" style={{ left: `${dbToPct(-18)}%` }} />
        <button
          type="button"
          className={`level-meter-clip ${clipped ? "is-on" : ""}`}
          onClick={onResetClip}
          disabled={!clipped || !onResetClip}
          aria-pressed={clipped}
          aria-label={clipped ? `${label} clipped — reset` : `${label} clip indicator`}
          title={clipped ? "Clipping — click to reset" : "Clip indicator"}
        />
      </div>
    </div>
  );
}
