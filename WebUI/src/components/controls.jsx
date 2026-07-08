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

export function Knob({ label, min, max, value, onChange, unit = "", step = "0.1", className = "", accent = "orange", decimals = 1, disabled = false }) {
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
    <div className={`knob-wrap knob-${accent} ${className} ${disabled ? "is-disabled" : ""}`.trim()}>
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
        {value.toFixed(decimals)}
        {unit ? ` ${unit}` : ""}
      </div>
    </div>
  );
}

export function BypassButton({ label, enabled, onToggle, className = "" }) {
  return (
    <button
      className={`bypass-button ${!enabled ? "active" : ""} ${className}`.trim()}
      type="button"
      onClick={onToggle}
      aria-label={label}
      title={label}
    />
  );
}
