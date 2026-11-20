import { Knob } from "./controls";
import { LevelMeter } from "./LevelMeter";
import { PracticeTools } from "./PracticeTools";

// Sticky quick-monitor bar (0059). Sits in the fixed, pre-scaled overlay layer
// (UiScale) and slides down once the Monitor section has scrolled past, so the
// input/output trims, the REC meter and the practice tools stay reachable
// without scrolling back up. Deliberately excludes BPM/tap and transport —
// the bar is a glanceable 56px strip, not a second control deck.
function formatDb(value) {
  return (Number(value) || 0).toFixed(1);
}

function QuickKnobCell({
  label,
  value,
  min,
  max,
  step,
  onChange,
  title,
  level,
  peak,
  clipped,
  onResetClip,
  meter = true,
}) {
  return (
    <div className="quick-cell">
      <Knob
        className="quick-knob"
        label={label}
        min={min}
        max={max}
        value={value}
        onChange={onChange}
        step={step}
        decimals={1}
        unit="dB"
        title={title}
        disableWheel
      />
      <div className="quick-cell-readout">
        <span className="quick-cell-label">{label}</span>
        <span className="quick-cell-value">
          {formatDb(value)}
          <i>dB</i>
        </span>
        {meter ? (
          <LevelMeter
            className="quick-meter"
            label={`${label} level`}
            level={level}
            peak={peak}
            clipped={clipped}
            onResetClip={onResetClip}
          />
        ) : null}
      </div>
    </div>
  );
}

function QuickMeterCell({ label, captureLive, level, peak, clipped, onResetClip }) {
  return (
    <div className="quick-cell quick-cell--meter">
      <div className="quick-cell-readout">
        <span className="quick-cell-label">
          <span className={`quick-lamp ${captureLive ? "is-on" : ""}`} aria-hidden="true" />
          {label}
        </span>
        <span className="quick-cell-value is-blank" aria-hidden="true">&nbsp;</span>
        <LevelMeter
          className="quick-meter"
          label={`${label} level`}
          level={level}
          peak={peak}
          clipped={clipped}
          onResetClip={onResetClip}
        />
      </div>
    </div>
  );
}

export function QuickBar({
  visible,
  input,
  onInputChange,
  output,
  onOutputChange,
  dryLevel,
  onDryLevelChange,
  transport,
  onResetClip,
  ...tools
}) {
  const resetClip = (target) => (onResetClip ? () => onResetClip(target) : undefined);

  // The bar lives outside the scroll container, so wheel events over it would
  // otherwise do nothing. Forward them to the page — except over a popover,
  // which owns its own scrolling (Chords is tall enough to scroll).
  const forwardWheel = (event) => {
    // Ctrl/Cmd + wheel is the interface zoom (0060); leave it to UiScale so
    // the gesture never both scrolls and zooms.
    if (event.ctrlKey || event.metaKey || event.deltaY === 0) return;
    if (event.target instanceof Element
        && event.target.closest(".tuner-popover, .scales-popover, .chords-popover, .shortcut-list"))
      return;
    const scroller = event.currentTarget.closest(".ui-viewport")?.querySelector(".ui-scroll");
    if (scroller) scroller.scrollTop += event.deltaY;
  };
  const captureLive = Boolean(
    transport?.recording
    || transport?.preRollActive
    || transport?.looperRecording
    || transport?.looperPreRoll,
  );

  return (
    <div
      className={`quick-bar ${visible ? "is-visible" : ""}`}
      role="region"
      aria-label="Quick monitor"
      onWheel={forwardWheel}
    >
      <div className="quick-bar-inner">
        <span className="section-index quick-bar-index" aria-hidden="true">01</span>
        <PracticeTools className="header-tools quick-tools" host="bar" transport={transport} {...tools} />
        <div className="quick-monitor" role="group" aria-label="Monitor controls">
          <QuickKnobCell
            label="Input"
            value={input}
            min={-12}
            max={24}
            step="0.5"
            onChange={onInputChange}
            title="Input trim (record level) — sets what goes into the chains and the capture"
            level={transport?.inputLevel}
            peak={transport?.inputPeak}
            clipped={transport?.inputClipped}
            onResetClip={resetClip("input")}
          />
          <QuickMeterCell
            label="REC"
            captureLive={captureLive}
            level={transport?.recordLevel}
            peak={transport?.recordPeak}
            clipped={transport?.recordClipped}
            onResetClip={resetClip("record")}
          />
          <QuickKnobCell
            label="Output"
            value={output}
            min={-60}
            max={12}
            step="0.5"
            onChange={onOutputChange}
            title="Master monitor output — changes what you hear, never what is recorded"
            level={transport?.outputLevel}
            peak={transport?.outputPeak}
            clipped={transport?.outputClipped}
            onResetClip={resetClip("output")}
          />
          <QuickKnobCell
            label="Dry"
            value={dryLevel}
            min={-60}
            max={0}
            step="0.5"
            onChange={onDryLevelChange}
            title="Direct dry pass-through — scales the raw input in the monitor and the capture"
            meter={false}
          />
        </div>
      </div>
    </div>
  );
}
