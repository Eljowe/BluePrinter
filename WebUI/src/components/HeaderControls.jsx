import { useState } from "react";
import { Knob } from "./controls";
import { LevelMeter } from "./LevelMeter";
import { IconX } from "./icons";

// Header-level monitoring knobs: tempo (shared by the take recorder,
// the looper and the MIDI clock), the input trim (record level) and the
// master monitor output, with the IN / REC / OUT meters that show what
// each stage is doing. The click and MIDI clock toggles live in the sync
// strip beside the recording tabs (SyncControls.jsx), so the header stays
// a clean row of knobs + meters.
function MeterChannel({ label, level, peak, clipped, onResetClip }) {
  return (
    <div className="monitor-meter">
      <LevelMeter
        label={`${label} level`}
        level={level}
        peak={peak}
        clipped={clipped}
        onResetClip={onResetClip}
      />
      <span className="monitor-meter-label">{label}</span>
    </div>
  );
}

// Ceiling + reset for the monitor/output soft-clip guard (0048), the same
// popover pattern as the Click-sound one in SyncControls. The header owns the
// open state: the Guard button toggles the guard, and the small caret beside
// it discloses this ceiling panel without changing the enabled state.
function GuardPopover({ ceiling, onCeilingChange, onClose }) {
  return (
    <div className="guard-popover" role="dialog" aria-label="Monitor guard settings">
      <div className="guard-popover-header">
        <span className="guard-popover-title">Monitor guard</span>
        <div className="guard-popover-actions">
          <button
            type="button"
            className="btn btn-ghost btn-sm"
            onClick={() => onCeilingChange(-1)}
            title="Reset the ceiling to the default -1.0 dBFS"
          >
            Reset
          </button>
          <button type="button" className="icon-btn" onClick={onClose} title="Close" aria-label="Close">
            <IconX size={13} />
          </button>
        </div>
      </div>

      <Knob
        label="Ceiling"
        min={-12}
        max={0}
        step="0.5"
        decimals={1}
        unit="dBFS"
        value={ceiling}
        onChange={onCeilingChange}
        title="Soft-clip ceiling — the monitor saturates towards this level instead of hard-clipping"
      />

      <p className="guard-popover-hint">
        Soft-clips the monitor at the ceiling. Never changes the print.
      </p>
    </div>
  );
}

export function HeaderControls({
  input,
  onInputChange,
  output,
  onOutputChange,
  bpm,
  onBpmChange,
  onTapTempo,
  dryLevel,
  onDryLevelChange,
  clipGuardEnabled,
  clipGuardCeiling,
  onClipGuardChange,
  onClipGuardCeilingChange,
  transport,
  onResetClip,
}) {
  const resetClip = (target) => (onResetClip ? () => onResetClip(target) : undefined);
  const [guardOpen, setGuardOpen] = useState(false);
  const guardEngaged = Boolean(clipGuardEnabled && transport?.outputClipped);
  const guardCeiling = Number.isFinite(Number(clipGuardCeiling)) ? Number(clipGuardCeiling) : -1;

  // The REC bus prints during a take or a loop capture (count-ins included),
  // so the lamp above the REC meter mirrors the Transport/Looper state pills.
  // Tap tempo is unavailable at the same times.
  const captureLive = Boolean(
    transport?.recording
    || transport?.preRollActive
    || transport?.looperRecording
    || transport?.looperPreRoll,
  );

  // Tap tempo (0049): a pointer-down tap (crisp timing) or Enter/Space on the
  // focused button. `tapPulse` remounts the ring so its animation restarts on
  // every tap.
  const [tapPulse, setTapPulse] = useState(0);
  const handleTap = () => {
    if (captureLive) return;
    setTapPulse((n) => n + 1);
    if (onTapTempo) onTapTempo();
  };
  const handleTapKeyDown = (event) => {
    if (event.key === "Enter" || event.key === " ") {
      event.preventDefault();
      handleTap();
    }
  };
  // Real clicks are handled on pointer-down; this catches assistive-tech and
  // programmatic activation, which dispatch a click with detail 0.
  const handleTapClick = (event) => {
    if (event.detail === 0) handleTap();
  };

  return (
    <div className="header-knobs">
      <div className="tempo-cell">
        <Knob
          label="BPM"
          min={40}
          max={240}
          value={bpm}
          onChange={onBpmChange}
          step="1"
          decimals={0}
          title="Tempo — shared by the take recorder, the looper and the MIDI clock"
        />
        <button
          type="button"
          className="tap-btn"
          aria-label="Tap tempo"
          disabled={captureLive}
          onPointerDown={(event) => { if (event.button === 0) handleTap(); }}
          onClick={handleTapClick}
          onKeyDown={handleTapKeyDown}
          title={captureLive
            ? "Tap tempo — unavailable while a take or loop capture is running"
            : "Tap tempo — click or press T in time with the beat"}
        >
          Tap
          {tapPulse > 0
            ? <span key={tapPulse} className="tap-pulse is-active" aria-hidden="true" />
            : null}
        </button>
      </div>
      <div className="monitor-cell">
        <Knob
          label="Input"
          min={-12}
          max={24}
          value={input}
          onChange={onInputChange}
          step="0.5"
          decimals={1}
          unit="dB"
          title="Input trim (record level) — sets what goes into the chains and the capture"
        />
        <MeterChannel
          label="IN"
          level={transport?.inputLevel}
          peak={transport?.inputPeak}
          clipped={transport?.inputClipped}
          onResetClip={resetClip("input")}
        />
      </div>
      <div className="monitor-cell monitor-cell--meter-only">
        <span
          className={`monitor-lamp ${captureLive ? "is-on" : ""}`}
          title="Lights while a take or loop capture is printing to the REC bus"
        >
          <span className="monitor-lamp-dot" aria-hidden="true" />
          Capture
        </span>
        <MeterChannel
          label="REC"
          level={transport?.recordLevel}
          peak={transport?.recordPeak}
          clipped={transport?.recordClipped}
          onResetClip={resetClip("record")}
        />
      </div>
      <div className="monitor-cell">
        <Knob
          label="Output"
          min={-60}
          max={12}
          value={output}
          onChange={onOutputChange}
          step="0.5"
          decimals={1}
          unit="dB"
          title="Master monitor output — changes what you hear, never what is recorded"
        />
        <MeterChannel
          label="OUT"
          level={transport?.outputLevel}
          peak={transport?.outputPeak}
          clipped={transport?.outputClipped}
          onResetClip={resetClip("output")}
        />
      </div>
      <div className="monitor-cell monitor-cell--guard">
        {/* Guard toggles the soft clip; the caret only discloses the ceiling. */}
        <div className="monitor-guard-row">
          <button
            type="button"
            className={`monitor-guard ${clipGuardEnabled ? "is-on" : ""} ${guardEngaged ? "is-engaged" : ""}`}
            aria-pressed={Boolean(clipGuardEnabled)}
            onClick={() => onClipGuardChange(!clipGuardEnabled)}
            title={clipGuardEnabled
              ? `Monitor guard is on — the monitor soft-clips at ${guardCeiling.toFixed(1)} dBFS and warns with the OUT clip LED when it limits. Never affects the print.`
              : "Monitor guard — soft-clip the monitor at a dBFS ceiling so a hot output saturates instead of hard-clipping. Never affects the print."}
          >
            <span className="monitor-guard-dot" aria-hidden="true" />
            Guard
          </button>
          <button
            type="button"
            className={`monitor-guard-caret ${guardOpen ? "is-open" : ""} ${clipGuardEnabled ? "is-on" : ""}`}
            aria-label="Monitor guard ceiling"
            aria-expanded={guardOpen}
            aria-haspopup="dialog"
            title="Guard ceiling"
            onClick={() => setGuardOpen((open) => !open)}
          >
            <span aria-hidden="true">▾</span>
          </button>
        </div>
        <span className="monitor-guard-value" title="Monitor guard ceiling">
          {guardCeiling.toFixed(1)} dBFS
        </span>
        {guardOpen ? (
          <GuardPopover
            ceiling={guardCeiling}
            onCeilingChange={onClipGuardCeilingChange}
            onClose={() => setGuardOpen(false)}
          />
        ) : null}
      </div>
      <Knob
        label="Dry"
        min={-60}
        max={0}
        value={dryLevel}
        onChange={onDryLevelChange}
        step="0.5"
        decimals={1}
        unit="dB"
        title="Direct dry pass-through — scales the raw input in the monitor and the capture. Turn it down to hear and record only the chains"
      />
    </div>
  );
}
