import { useEffect, useRef } from "react";

// Reference pitches offered for A4. The backend clamps to 400..480.
const REFERENCES = [432, 435, 438, 440, 441, 442];

// Header tuner popover (0034). Shows the note + cents from the worker's live
// estimate (computed in C++ and shipped in the transport snapshot), a
// reference-pitch selector and a monitor-only mute. Opening it starts the
// backend analysis worker.
export function Tuner({ transport, onReferenceChange, onMonitorMuteChange, onClose }) {
  const reference = Number(transport?.tunerReferencePitch ?? 440);
  const frequency = Number(transport?.tunerFrequency ?? 0);
  const confidence = Number(transport?.tunerConfidence ?? 0);
  const monitorMute = Boolean(transport?.tunerMonitorMute);
  const note = transport?.tunerNote ?? "";
  const cents = Number(transport?.tunerCents ?? 0);
  const reading = frequency > 0 && confidence > 0.5 && note !== "";
  const inTune = reading && Math.abs(cents) <= 5;

  // Damped needle: ease the displayed cents toward the latest push on a rAF
  // clock and write straight to the DOM (no per-frame React render), matching
  // the playhead approach in Looper.jsx.
  const needleRef = useRef(null);
  const displayRef = useRef(0);
  const targetRef = useRef(0);
  const readingRef = useRef(false);
  targetRef.current = reading ? Math.max(-50, Math.min(50, cents)) : 0;
  readingRef.current = reading;

  useEffect(() => {
    let raf = 0;
    const tick = () => {
      displayRef.current += (targetRef.current - displayRef.current) * 0.2;
      const node = needleRef.current;
      if (node) {
        node.style.left = `${50 + (displayRef.current / 50) * 50}%`;
        node.style.opacity = readingRef.current ? "1" : "0";
      }
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, []);

  // Focus the popover on open and close on Escape (0017 keyboard conventions).
  const popoverRef = useRef(null);
  useEffect(() => {
    popoverRef.current?.focus();
  }, []);

  return (
    <div
      className="tuner-popover"
      role="dialog"
      aria-label="Tuner"
      tabIndex={-1}
      ref={popoverRef}
      onKeyDown={(e) => {
        if (e.key === "Escape") {
          e.stopPropagation();
          if (onClose) onClose();
        }
      }}
    >
      <div className="tuner-popover-header">
        <span className="tuner-popover-title">Tuner</span>
        <label className="tuner-ref" title="Reference pitch for A4">
          <span className="tuner-ref-label">Ref</span>
          <select
            className="tuner-ref-select"
            value={reference}
            onChange={(e) => onReferenceChange(Number(e.target.value))}
          >
            {REFERENCES.map((hz) => <option key={hz} value={hz}>{hz} Hz</option>)}
          </select>
        </label>
        <button
          type="button"
          className={`tuner-mute ${monitorMute ? "is-on" : ""}`}
          onClick={() => onMonitorMuteChange(!monitorMute)}
          aria-pressed={monitorMute}
          title="Silence the monitor while tuning — never changes what is captured"
        >
          Mute monitor
        </button>
      </div>

      <div
        className={`tuner-readout ${reading ? (inTune ? "is-intune" : "") : "is-idle"}`}
        aria-live="polite"
        aria-label="Detected pitch"
      >
        <span className="tuner-note">{reading ? note : "—"}</span>
        <span className="tuner-cents">
          {reading ? `${cents >= 0 ? "+" : ""}${cents.toFixed(0)}¢` : ""}
        </span>
      </div>

      <div className="tuner-scale" aria-hidden="true">
        <span className="tuner-scale-center" />
        <span className="tuner-needle" ref={needleRef} style={{ opacity: 0 }} />
      </div>

      <p className="tuner-hint">
        {reading ? `≈ ${frequency.toFixed(1)} Hz` : "Play a note…"}
      </p>
    </div>
  );
}
