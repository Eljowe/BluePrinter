import { useRef } from "react";

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

// Angle geometry for the value arc: the sweep runs from -140° to +140°
// (0° = straight up), leaving a gap at the bottom of the dial.
const ARC_START = -140;
const ARC_SWEEP = 280;

function polar(cx, cy, r, angleDeg) {
  const a = ((angleDeg - 90) * Math.PI) / 180;
  return [cx + r * Math.cos(a), cy + r * Math.sin(a)];
}

function arcPath(cx, cy, r, startAngle, endAngle) {
  const [sx, sy] = polar(cx, cy, r, startAngle);
  const [ex, ey] = polar(cx, cy, r, endAngle);
  const largeArc = Math.abs(endAngle - startAngle) > 180 ? 1 : 0;
  return `M ${sx} ${sy} A ${r} ${r} 0 ${largeArc} 1 ${ex} ${ey}`;
}

export function Knob({ label, min, max, value, onChange, unit = "", step = "0.01", className = "", decimals = 1, disabled = false }) {
  const normalized = clamp((value - min) / (max - min), 0, 1);
  const angle = ARC_START + normalized * ARC_SWEEP;
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

  const [tipX, tipY] = polar(24, 24, 12.5, angle);
  const [baseX, baseY] = polar(24, 24, 5.5, angle);

  return (
    <div className={`knob-wrap ${className} ${disabled ? "is-disabled" : ""}`.trim()}>
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
        <svg viewBox="0 0 48 48" className="knob-dial">
          <path
            className="knob-track"
            d={arcPath(24, 24, 19, ARC_START, ARC_START + ARC_SWEEP)}
          />
          {normalized > 0.001 ? (
            <path
              className="knob-arc"
              d={arcPath(24, 24, 19, ARC_START, angle)}
            />
          ) : null}
          <circle className="knob-face" cx="24" cy="24" r="13.5" />
          <line className="knob-pointer" x1={baseX} y1={baseY} x2={tipX} y2={tipY} />
          <circle className="knob-tip" cx={tipX} cy={tipY} r="1.6" />
        </svg>
      </div>
      <div className="knob-value">
        {value.toFixed(decimals)}
        {unit ? <span className="knob-unit">{` ${unit}`}</span> : null}
      </div>
      <div className="knob-label">{label}</div>
    </div>
  );
}
