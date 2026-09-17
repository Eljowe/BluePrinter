import { useEffect, useMemo, useRef, useState } from "react";

const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const MIN_ZOOM = 1;
const MAX_ZOOM = 24;

const clamp = (value, lo, hi) => Math.min(hi, Math.max(lo, value));

export function midiToNoteName(midi) {
  const value = Number(midi);
  if (!Number.isFinite(value)) return "";
  const name = NOTE_NAMES[((value % 12) + 12) % 12];
  return `${name}${Math.floor(value / 12) - 1}`;
}

function describeNote(bar, sampleRate) {
  const parts = [midiToNoteName(bar.midi)];
  if (sampleRate > 0) {
    parts.push(`${(bar.start / sampleRate).toFixed(2)}s`);
    parts.push(`${Math.round((bar.length / sampleRate) * 1000)}ms`);
  }
  if (Math.abs(bar.cents) >= 1) {
    parts.push(`${bar.cents > 0 ? "+" : ""}${bar.cents.toFixed(0)}¢`);
  }
  return parts.join(" · ");
}

function niceStep(raw) {
  const steps = [0.05, 0.1, 0.2, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120];
  for (const step of steps) if (step >= raw) return step;
  return steps[steps.length - 1];
}

function formatSeconds(value) {
  return Number.isInteger(value) ? `${value}s` : `${value.toFixed(2)}s`;
}

// Interactive piano-roll for a take's extracted melody (0054): y = pitch,
// x = time. Scroll to zoom, shift-scroll to pan, double-click to fit; hover or
// focus a note to read it, click (or Enter) to audition the melody from it.
export function PianoRoll({
  notes = [],
  totalSamples = 0,
  sampleRate = 0,
  position = 0,
  playing = false,
  keySignature = "",
  bpm = 0,
  beatsPerBar = 4,
  onAudition,
  height = 96,
}) {
  const [zoom, setZoom] = useState(1);
  const [pan, setPan] = useState(0);
  const [hovered, setHovered] = useState(-1);
  const [selected, setSelected] = useState(-1);

  const plotRef = useRef(null);
  const buttonRefs = useRef([]);

  const bars = useMemo(
    () => (Array.isArray(notes) ? notes : [])
      .map((n) => ({
        start: Number(n.startSample) || 0,
        length: Number(n.lengthSamples) || 0,
        midi: Number(n.midi),
        cents: Number(n.cents) || 0,
      }))
      .filter((n) => n.length > 0 && Number.isFinite(n.midi)),
    [notes],
  );

  const total = useMemo(() => {
    const explicit = Number(totalSamples) > 0 ? Number(totalSamples) : 0;
    return explicit || bars.reduce((max, b) => Math.max(max, b.start + b.length), 0);
  }, [totalSamples, bars]);

  const { minMidi, maxMidi, span } = useMemo(() => {
    let lo = Infinity;
    let hi = -Infinity;
    for (const bar of bars) {
      lo = Math.min(lo, bar.midi);
      hi = Math.max(hi, bar.midi);
    }
    if (!Number.isFinite(lo)) return { minMidi: 59, maxMidi: 65, span: 7 };
    if (hi - lo < 5) {
      const centre = (lo + hi) / 2;
      lo = Math.floor(centre - 3);
      hi = Math.ceil(centre + 3);
    }
    lo -= 1;
    hi += 1;
    return { minMidi: lo, maxMidi: hi, span: Math.max(1, hi - lo) };
  }, [bars]);

  const viewSpan = 1 / zoom;
  const viewStart = clamp(pan, 0, Math.max(0, 1 - viewSpan));

  const resetView = () => { setZoom(1); setPan(0); };

  // Non-passive wheel: scroll = zoom about the cursor, shift = pan.
  useEffect(() => {
    const el = plotRef.current;
    if (!el) return undefined;

    const handler = (event) => {
      event.preventDefault();
      if (event.shiftKey) {
        const dir = event.deltaY > 0 ? 1 : -1;
        setPan((p) => clamp(p + dir * viewSpan * 0.2, 0, Math.max(0, 1 - viewSpan)));
        return;
      }
      const rect = el.getBoundingClientRect();
      const cursorFrac = clamp((event.clientX - rect.left) / Math.max(1, rect.width), 0, 1);
      const cursorTime = viewStart + cursorFrac * viewSpan;
      const nextZoom = clamp(zoom * (event.deltaY < 0 ? 1.25 : 0.8), MIN_ZOOM, MAX_ZOOM);
      const nextSpan = 1 / nextZoom;
      setZoom(nextZoom);
      setPan(clamp(cursorTime - cursorFrac * nextSpan, 0, Math.max(0, 1 - nextSpan)));
    };

    el.addEventListener("wheel", handler, { passive: false });
    return () => el.removeEventListener("wheel", handler);
  }, [viewStart, viewSpan, zoom]);

  // While auditioning, keep the playhead in view when zoomed in.
  useEffect(() => {
    if (!playing || total <= 0 || zoom <= 1) return;
    const frac = position / total;
    if (frac < viewStart || frac > viewStart + viewSpan) {
      setPan(clamp(frac - viewSpan / 2, 0, Math.max(0, 1 - viewSpan)));
    }
  }, [playing, position, total, zoom, viewStart, viewSpan]);

  if (bars.length === 0) {
    return (
      <div className="piano-roll piano-roll-empty" aria-label="Melody piano roll — no notes yet">
        <span>No melody analysed yet</span>
      </div>
    );
  }

  const durationSec = sampleRate > 0 ? total / sampleRate : 0;
  const toView = (frac) => (frac - viewStart) / viewSpan;

  const noteNames = Array.from(new Set(bars.map((b) => midiToNoteName(b.midi))));
  const summary = `${bars.length} note${bars.length === 1 ? "" : "s"}${
    keySignature ? `, key ${keySignature}` : ""
  }: ${noteNames.join(", ")}`;

  const items = bars.map((bar) => ({
    ...bar,
    xFrac: total > 0 ? bar.start / total : 0,
    wFrac: total > 0 ? bar.length / total : 0,
    row: maxMidi - bar.midi,
  }));

  const rowLabels = [];
  const seenRows = new Set();
  for (const item of items) {
    if (!seenRows.has(item.row)) {
      seenRows.add(item.row);
      rowLabels.push({ row: item.row, midi: item.midi });
    }
  }
  rowLabels.sort((a, b) => a.row - b.row);

  const activeIndex = hovered >= 0 ? hovered : selected;
  const active = activeIndex >= 0 ? items[activeIndex] : null;
  const playheadView = playing && total > 0 ? toView(position / total) : null;
  const playheadVisible = playheadView !== null && playheadView >= 0 && playheadView <= 1;

  const audition = (item) => {
    if (typeof onAudition === "function") onAudition(item.start);
  };

  const focusNote = (index) => {
    const clamped = Math.max(0, Math.min(items.length - 1, index));
    setSelected(clamped);
    const ref = buttonRefs.current[clamped];
    if (ref) ref.focus();
  };

  const handleNoteKeyDown = (event, index) => {
    if (event.key === "ArrowRight" || event.key === "ArrowDown") {
      event.preventDefault();
      focusNote(index + 1);
    } else if (event.key === "ArrowLeft" || event.key === "ArrowUp") {
      event.preventDefault();
      focusNote(index - 1);
    } else if (event.key === "Home") {
      event.preventDefault();
      focusNote(0);
    } else if (event.key === "End") {
      event.preventDefault();
      focusNote(items.length - 1);
    } else if (event.key === "Enter" || event.key === " ") {
      event.preventDefault();
      setSelected(index);
      audition(items[index]);
    }
  };

  const handlePlotKeyDown = (event) => {
    if (event.key === "+" || event.key === "=") {
      event.preventDefault();
      setZoom((z) => clamp(z * 1.25, MIN_ZOOM, MAX_ZOOM));
    } else if (event.key === "-" || event.key === "_") {
      event.preventDefault();
      setZoom((z) => {
        const next = clamp(z * 0.8, MIN_ZOOM, MAX_ZOOM);
        if (next <= MIN_ZOOM) setPan(0);
        return next;
      });
    } else if (event.key === "0") {
      event.preventDefault();
      resetView();
    }
  };

  // Time ruler ticks.
  const ticks = [];
  if (durationSec > 0) {
    const viewSecStart = viewStart * durationSec;
    const viewSecSpan = viewSpan * durationSec;
    const step = niceStep(viewSecSpan / 6);
    for (let t = Math.floor(viewSecStart / step) * step; t <= viewSecStart + viewSecSpan + 1e-9; t += step) {
      const frac = t / durationSec;
      const x = toView(frac);
      if (x >= -0.001 && x <= 1.001) ticks.push({ x, label: formatSeconds(Number(t.toFixed(4))) });
    }
  }

  // Beat grid (only when a tempo is known).
  const beatLines = [];
  if (bpm > 0 && durationSec > 0) {
    const beatSec = 60 / bpm;
    const viewSecStart = viewStart * durationSec;
    const viewSecEnd = (viewStart + viewSpan) * durationSec;
    const first = Math.ceil(viewSecStart / beatSec - 1e-9);
    for (let i = first; i * beatSec <= viewSecEnd + 1e-9; i += 1) {
      const x = toView((i * beatSec) / durationSec);
      if (x >= 0 && x <= 1) beatLines.push({ x, major: i % beatsPerBar === 0 });
    }
  }

  return (
    <div className="piano-roll">
      <div className="piano-roll-axis" aria-hidden="true">
        {rowLabels.map((rl) => (
          <span key={rl.row} style={{ top: `${((rl.row + 0.5) / span) * 100}%` }}>
            {midiToNoteName(rl.midi)}
          </span>
        ))}
      </div>

      <div className="piano-roll-main">
        <div className="piano-roll-ruler" aria-hidden="true">
          {ticks.map((tick, index) => (
            <span key={index} style={{ left: `${tick.x * 100}%` }}>{tick.label}</span>
          ))}
        </div>

        <div
          className="piano-roll-plot"
          ref={plotRef}
          role="group"
          aria-label={`Melody piano roll, ${summary}. Scroll to zoom, shift-scroll to pan`}
          tabIndex={0}
          onKeyDown={handlePlotKeyDown}
          onDoubleClick={resetView}
        >
          <svg viewBox={`0 0 1000 ${height}`} preserveAspectRatio="none" className="piano-roll-svg" aria-hidden="true">
            {beatLines.map((line, index) => (
              <line
                key={`beat-${index}`}
                className={`piano-roll-beat ${line.major ? "is-bar" : ""}`}
                x1={line.x * 1000}
                y1={0}
                x2={line.x * 1000}
                y2={height}
                strokeWidth={1}
                vectorEffect="non-scaling-stroke"
              />
            ))}
            {items.map((item, index) => {
              const x = toView(item.xFrac) * 1000;
              const w = (item.wFrac / viewSpan) * 1000;
              const detuned = Math.abs(item.cents) > 25;
              const isActive = index === activeIndex;
              return (
                <rect
                  key={index}
                  className={`piano-roll-note ${detuned ? "is-detuned" : ""} ${isActive ? "is-active" : ""}`}
                  x={x}
                  y={(item.row + 0.15) * (height / span)}
                  width={Math.max(1, w)}
                  height={Math.max(1, (height / span) * 0.7)}
                  rx={Math.min(2, (height / span) * 0.2)}
                />
              );
            })}
            {playheadVisible ? (
              <line
                className="piano-roll-playhead"
                x1={playheadView * 1000}
                y1={0}
                x2={playheadView * 1000}
                y2={height}
                strokeWidth={1}
                vectorEffect="non-scaling-stroke"
              />
            ) : null}
          </svg>

          <div className="piano-roll-hits">
            {items.map((item, index) => {
              const left = toView(item.xFrac) * 100;
              const w = Math.max((item.wFrac / viewSpan) * 100, 1.2);
              const showLabel = zoom >= 3 && w * 1.2 >= 6;
              return (
                <button
                  type="button"
                  key={index}
                  ref={(el) => { buttonRefs.current[index] = el; }}
                  className={`piano-roll-hit ${index === selected ? "is-selected" : ""}`}
                  style={{
                    left: `${left}%`,
                    width: `${w}%`,
                    top: `${((item.row + 0.15) / span) * 100}%`,
                    height: `${(0.7 / span) * 100}%`,
                  }}
                  aria-label={`Note ${midiToNoteName(item.midi)}, ${describeNote(item, sampleRate)}. Enter to audition from here`}
                  title={describeNote(item, sampleRate)}
                  onMouseEnter={() => setHovered(index)}
                  onMouseLeave={() => setHovered((h) => (h === index ? -1 : h))}
                  onFocus={() => setHovered(index)}
                  onBlur={() => setHovered((h) => (h === index ? -1 : h))}
                  onKeyDown={(e) => handleNoteKeyDown(e, index)}
                  onClick={() => {
                    setSelected((s) => (s === index ? -1 : index));
                    audition(item);
                  }}
                >
                  {showLabel ? (
                    <span className="piano-roll-hit-label" aria-hidden="true">{midiToNoteName(item.midi)}</span>
                  ) : null}
                </button>
              );
            })}
          </div>

          {active ? <div className="piano-roll-tip">{describeNote(active, sampleRate)}</div> : null}

          <div className="piano-roll-zoom">
            <button type="button" onClick={() => setZoom((z) => clamp(z * 0.8, MIN_ZOOM, MAX_ZOOM))} disabled={zoom <= MIN_ZOOM} aria-label="Zoom out">−</button>
            <button type="button" onClick={resetView} disabled={zoom <= 1 && pan <= 0} aria-label="Fit melody">Fit</button>
            <button type="button" onClick={() => setZoom((z) => clamp(z * 1.25, MIN_ZOOM, MAX_ZOOM))} disabled={zoom >= MAX_ZOOM} aria-label="Zoom in">+</button>
          </div>
        </div>
      </div>
    </div>
  );
}
