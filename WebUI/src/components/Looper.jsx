import { useCallback, useEffect, useLayoutEffect, useRef, useState } from "react";
import { FRONTEND_EVENTS, emit } from "../bridge";
import { Knob } from "./controls";
import { IconPlay, IconSave, IconStop, IconTrash } from "./icons";
import { LevelMeter } from "./LevelMeter";
import { Waveform } from "./Waveform";

// Shared with the transport: count-in field with a "beats" suffix.
function NumberInput({ value, min, max, step, className, onChange, suffix, title }) {
  const [text, setText] = useState(String(value));
  const committedRef = useRef(value);

  useEffect(() => {
    if (value !== committedRef.current) {
      setText(String(value));
      committedRef.current = value;
    }
  }, [value]);

  const flush = useCallback((raw) => {
    const parsed = parseInt(raw, 10);
    if (!Number.isNaN(parsed)) {
      const clamped = Math.max(min, Math.min(max, parsed));
      committedRef.current = clamped;
      onChange(clamped);
      setText(String(clamped));
    } else {
      setText(String(committedRef.current));
    }
  }, [min, max, onChange]);

  return (
    <div className={className}>
      <input
        type="number"
        min={min}
        max={max}
        step={step}
        value={text}
        onChange={(e) => setText(e.target.value)}
        onBlur={(e) => flush(e.target.value)}
        onKeyDown={(e) => { if (e.key === "Enter") e.currentTarget.blur(); }}
        title={title}
      />
      {suffix ? <span className="count-in-suffix">{suffix}</span> : null}
    </div>
  );
}

function Stepper({ label, value, min, max, onChange, title }) {
  const step = (delta) => {
    const next = Math.max(min, Math.min(max, value + delta));
    if (next !== value) onChange(next);
  };
  return (
    <div className="looper-stepper" title={title}>
      <span className="looper-stepper-label">{label}</span>
      <div className="looper-stepper-control">
        <button type="button" className="looper-stepper-btn" disabled={value <= min} onClick={() => step(-1)} aria-label={`${label}: decrease`}>−</button>
        <span className="looper-stepper-value">{value}</span>
        <button type="button" className="looper-stepper-btn" disabled={value >= max} onClick={() => step(1)} aria-label={`${label}: increase`}>+</button>
      </div>
    </div>
  );
}

// Formats a beat count for captions: whole bars when possible, beats
// otherwise (crop is beat-granular).
function formatBeats(beats) {
  if (beats <= 0) return "0 beats";
  if (beats % 4 === 0) {
    const bars = beats / 4;
    return `${bars} bar${bars === 1 ? "" : "s"}`;
  }
  return `${beats} beats`;
}

export function Looper({ transport, onOverdubChange, onLoopLevelChange, onOverdubLevelChange, onResetClip }) {
  const recording = Boolean(transport?.looperRecording);
  const preRoll = Boolean(transport?.looperPreRoll);
  const isRecording = recording || preRoll;
  const playing = Boolean(transport?.looperPlaying);
  const looping = transport?.looperLooping !== false;
  const overdub = Boolean(transport?.looperOverdub);
  const lengthBars = Number(transport?.looperLengthBars ?? 0);
  const fixedBars = lengthBars > 0;

  const loopLength = Number(transport?.audioLoopLength ?? 0);
  const hasLoop = loopLength > 0;

  // Playhead animation state: the marker is interpolated on a rAF clock
  // between the 30 Hz transport pushes (see the effect below), so it never
  // re-renders React at frame rate. `deadband` refs keep it from nudging
  // backwards on push latency; the timer drives the seam fade.
  const playheadRef = useRef(null);
  const clockRef = useRef({ pos: 0, at: 0 });
  const lastPosRef = useRef(0);
  const seamTimerRef = useRef(null);

  // A capture layers over the loop when overdub is on, looping is on and
  // a loop exists — matches the backend's condition at capture start.
  const isOverdubbing = isRecording && overdub && hasLoop;
  const countInBeats = Number(transport?.looperCountInBeats ?? 0);
  const cropStartBeats = Number(transport?.looperCropStartBeats ?? 0);
  const cropEndBeats = Number(transport?.looperCropEndBeats ?? 0);
  const loopStart = Number(transport?.audioLoopStart ?? 0);

  // The captured loop is trimmed to whole bars, so the beat count can be
  // derived from the current BPM/sample rate.
  const beatSamples = Number(transport?.bpm ?? 120) > 0 && Number(transport?.recordingSampleRate ?? 0) > 0
    ? (60.0 / Number(transport.bpm)) * Number(transport.recordingSampleRate)
    : 0;

  // Fresh-capture progress: the timeline fills as the capture grows toward
  // the record buffer's capacity — or, in fixed-length mode, toward the
  // chosen bar count so the auto-stop is visible.
  const captureMax = Number(transport?.maxRecordSamples ?? 0);
  const captureTarget = fixedBars && beatSamples > 0 ? lengthBars * 4 * beatSamples : 0;
  const captureDenominator = fixedBars && captureTarget > 0 ? captureTarget : captureMax;
  const capturePct = captureDenominator > 0 && isRecording && !isOverdubbing
    ? Math.min(100, Math.max(0, (loopLength / captureDenominator) * 100))
    : 0;

  // The cropped window + the cropped-off beats are the FULL loop, which
  // is what the crop beats are measured against in the backend — so the
  // stepper ranges and the crop overlays stay anchored and moving a crop
  // handle back toward 0 restores the region it cut off instead of
  // shrinking the window further.
  const totalBeats = beatSamples > 0 && loopLength > 0
    ? Math.max(1, Math.round(loopLength / beatSamples) + cropStartBeats + cropEndBeats)
    : 0;
  const croppedBeats = totalBeats > 0 ? Math.max(0, totalBeats - cropStartBeats - cropEndBeats) : 0;

  // X-axis ruler: a tick per beat with the bar boundaries emphasised, and a
  // number under each bar. Long loops thin the numbers out so they never
  // collide, and drop the per-beat ticks once they'd read as noise.
  const totalBars = totalBeats > 0 ? Math.ceil(totalBeats / 4) : 0;
  const showBeatTicks = totalBeats > 0 && totalBeats <= 32;
  const barLabelStep = totalBars > 24 ? 4 : totalBars > 12 ? 2 : 1;
  const barNumberLabels = [];
  for (let bar = 0; bar < totalBars; bar += barLabelStep) {
    const centerBeat = bar * 4 + 2;
    if (centerBeat > totalBeats) break;
    const pct = (centerBeat / totalBeats) * 100;
    if (pct > 97) break;
    barNumberLabels.push({ bar: bar + 1, pct });
  }

  // The waveform shows the FULL loop, so the playhead maps the absolute
  // window position onto the full sample range.
  const fullSamples = beatSamples > 0 && totalBeats > 0 ? totalBeats * beatSamples : loopLength;
  const cropStartPct = totalBeats > 0 ? (cropStartBeats / totalBeats) * 100 : 0;
  const cropEndPct = totalBeats > 0 ? (cropEndBeats / totalBeats) * 100 : 0;

  // Count-in countdown, identical to the take transport: the pre-roll
  // advances transportPosition in processBlock, so the beat boundary is
  // derived from the shared BPM/sample rate and the remaining beats
  // count down from the configured count-in length.
  let countdown = null;
  if (preRoll && Number(transport?.bpm ?? 0) > 0 && Number(transport?.recordingSampleRate ?? 0) > 0) {
    const samplesPerBeat = (60.0 / Number(transport.bpm)) * Number(transport.recordingSampleRate);
    const currentBeat = Math.floor((Number(transport.transportPosition ?? 0)) / samplesPerBeat);
    countdown = Math.max(1, countInBeats - currentBeat);
  }

  // Animate the playhead by extrapolating from the last transport push at
  // the known sample rate. The old `left` CSS transition trailed the audio
  // by ~80 ms; the rAF clock tracks it smoothly and resets straight to the
  // window start at the seam (no backwards sweep). The position is written
  // straight to the DOM so React doesn't re-render at frame rate.
  useLayoutEffect(() => {
    const node = playheadRef.current;
    if (!node || !hasLoop) return undefined;

    const sampleRate = Number(transport?.recordingSampleRate ?? 0);
    const snapshotPos = Number(transport?.audioLoopPosition ?? 0);
    const toPct = (pos) => (fullSamples > 0
      ? Math.min(100, Math.max(0, ((loopStart + pos) / fullSamples) * 100))
      : 0);

    if (!playing || sampleRate <= 0 || loopLength <= 0) {
      node.style.left = `${toPct(snapshotPos)}%`;
      node.classList.remove("is-seam");
      return undefined;
    }

    // Re-seed the clock baseline only when the push disagrees with our
    // extrapolation by more than the deadband — a few ms of push latency
    // would otherwise pull the marker a hair backwards every tick.
    const now = performance.now();
    const clock = clockRef.current;
    let elapsed = clock.at > 0 ? ((now - clock.at) / 1000) * sampleRate : 0;
    const deadband = sampleRate * 0.02;
    if (clock.at === 0 || Math.abs(clock.pos + elapsed - snapshotPos) > deadband) {
      clock.pos = snapshotPos;
      clock.at = now;
      elapsed = 0;
    }
    lastPosRef.current = looping
      ? (clock.pos + elapsed) % loopLength
      : Math.min(clock.pos + elapsed, loopLength);

    let raf = 0;
    const tick = () => {
      const c = clockRef.current;
      let pos = c.pos + ((performance.now() - c.at) / 1000) * sampleRate;
      if (looping) {
        pos %= loopLength;
        // A drop of more than half the loop means the phase wrapped; fade
        // the marker briefly so the reset reads as a cycle, not a glitch.
        if (lastPosRef.current - pos > loopLength * 0.5) {
          node.classList.add("is-seam");
          window.clearTimeout(seamTimerRef.current);
          seamTimerRef.current = window.setTimeout(
            () => node.classList.remove("is-seam"), 80);
        }
      } else {
        pos = Math.min(pos, loopLength);
      }
      lastPosRef.current = pos;
      node.style.left = `${toPct(pos)}%`;
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [playing, hasLoop, looping, loopLength, loopStart, fullSamples, transport?.audioLoopPosition, transport?.recordingSampleRate]);

  // Clear a pending seam-fade timer if the component goes away.
  useEffect(() => () => window.clearTimeout(seamTimerRef.current), []);

  const setRecording = (enabled) => emit(FRONTEND_EVENTS.setLooperRecording, { enabled });
  const setPlaying = (enabled) => emit(FRONTEND_EVENTS.setLooperPlaying, { enabled });
  const emitCrop = (startBeats, endBeats) => emit(FRONTEND_EVENTS.setLoopCrop, { startBeats, endBeats });

  return (
    <section className={`looper ${isRecording ? "is-recording" : ""} ${playing ? "is-playing" : ""} ${preRoll ? "is-counting-in" : ""}`}>
      <div className="looper-header">
        <div>
          <h2>Capture a loop. Play over it.</h2>
          <p>Records whatever the chains make — synth, guitar, FX — so the loop sounds exactly like what you heard.</p>
        </div>
        <div className="looper-state" aria-live="polite">
          <span className="looper-state-dot" />
          {preRoll ? "Count-in" : isOverdubbing ? "Overdub" : recording ? (fixedBars ? `Recording · ${lengthBars} bar${lengthBars === 1 ? "" : "s"}` : "Recording") : playing ? "Playing" : hasLoop ? `${formatBeats(croppedBeats)} loop ready` : "Empty"}
        </div>
      </div>

      <div className={`looper-timeline ${preRoll ? "is-counting-in" : ""}`} aria-label={`${formatBeats(croppedBeats)} loop`}>
        <div
          className="looper-ruler"
          style={hasLoop && totalBeats > 0 ? {
            "--beat-width": showBeatTicks ? `${100 / totalBeats}%` : `${400 / totalBeats}%`,
            "--bar-width": `${400 / totalBeats}%`,
          } : undefined}
        />
        {isRecording && !isOverdubbing && capturePct > 0 ? (
          <div className="looper-capture-progress" style={{ width: `${capturePct}%` }} />
        ) : null}
        {hasLoop ? (
          <div className="looper-waveform">
            <Waveform peaks={transport.audioLoopPeaks ?? []} width={360} height={88} />
          </div>
        ) : null}
        {preRoll && countdown != null ? (
          <div className="looper-countdown" key={countdown} aria-hidden="true">
            {countdown}
          </div>
        ) : null}
        {hasLoop ? <div className="looper-crop-left" style={{ width: `${cropStartPct}%` }} /> : null}
        {hasLoop ? <div className="looper-crop-right" style={{ width: `${cropEndPct}%` }} /> : null}
        {hasLoop ? <div ref={playheadRef} className="looper-playhead" /> : null}
        {hasLoop ? (
          <div className="looper-ruler-labels" aria-hidden="true">
            {barNumberLabels.map((b) => (
              <span key={b.bar} className="looper-ruler-label" style={{ left: `${b.pct}%` }}>{b.bar}</span>
            ))}
          </div>
        ) : null}
        <div className="looper-timeline-caption">
          <span>{hasLoop ? formatBeats(croppedBeats) : "No loop captured yet"}</span>
          <span>{looping ? "LOOP" : "ONE SHOT"}</span>
        </div>
      </div>

      <div className="looper-controls">
        <div className="looper-primary-controls">
          <button
            type="button"
            className={`looper-record-button ${isRecording ? "is-active" : ""}`}
            onClick={() => setRecording(!isRecording)}
            title={isRecording ? "Stop loop recording" : "Record loop"}
            aria-pressed={isRecording}
            aria-label={isRecording ? "Stop loop recording" : "Record loop"}
          >
            <span className="looper-record-dot" aria-hidden="true" />
          </button>
          <div className="looper-meter" aria-hidden={!isRecording}>
            <LevelMeter
              label="Input level"
              level={transport?.inputLevel ?? 0}
              peak={transport?.inputPeak ?? 0}
              clipped={Boolean(transport?.inputClipped)}
              onResetClip={onResetClip ? () => onResetClip("input") : undefined}
            />
          </div>
          <button type="button" className="btn btn-primary btn-sm" disabled={!hasLoop} onClick={() => setPlaying(!playing)}>
            {playing ? <IconStop size={13} /> : <IconPlay size={13} />} {playing ? "Stop loop" : "Play loop"}
          </button>
          <button
            type="button"
            className="btn btn-ghost btn-sm"
            disabled={!hasLoop && !isRecording}
            onClick={() => emit(FRONTEND_EVENTS.clearLoop)}
            title="Delete the loop without saving"
          >
            <IconTrash size={13} /> Discard
          </button>
          <button type="button" className="btn btn-ghost btn-sm" disabled={!hasLoop} onClick={() => emit(FRONTEND_EVENTS.saveLoop)}>
            <IconSave size={13} /> Save to library
          </button>
        </div>

        <div className="looper-settings">
          <div className="looper-setting looper-setting--levels">
            <div className="looper-level-cell">
              <Knob
                label="Loop"
                min={-60}
                max={12}
                value={Number(transport?.loopLevel ?? 0)}
                onChange={onLoopLevelChange}
                step="0.5"
                decimals={1}
                unit="dB"
                className="looper-level-knob"
                title="Loop playback level (monitor only) — pull it down during an overdub to hear your new layer"
              />
              <LevelMeter
                label="Loop playback level"
                level={transport?.loopPlayLevel ?? 0}
                peak={transport?.loopPlayPeak ?? 0}
                clipped={Boolean(transport?.loopClipped)}
                onResetClip={onResetClip ? () => onResetClip("loop") : undefined}
              />
            </div>

            <Knob
              label="Dub"
              min={-60}
              max={0}
              value={Number(transport?.overdubLevel ?? 0)}
              onChange={onOverdubLevelChange}
              step="0.5"
              decimals={1}
              unit="dB"
              className="looper-level-knob"
              title="Overdub trim — attenuates each new layer before it is mixed into the loop (0 dB = no change)"
            />
          </div>

          <div className="looper-setting">
            <label className="count-in-control">
              <span className="count-in-label">Count-in</span>
              <NumberInput
                className="count-in-field"
                min={0}
                max={8}
                step={1}
                value={countInBeats}
                onChange={(beats) => emit(FRONTEND_EVENTS.setLooperCountIn, { beats })}
                suffix="beats"
                title="Beats of click before the loop capture starts (0 = off)"
              />
            </label>
          </div>

          <div className="looper-setting">
            <label className="count-in-control">
              <span className="count-in-label">Length</span>
              <select
                className="looper-length-select"
                value={fixedBars ? String(lengthBars) : "0"}
                onChange={(e) => emit(FRONTEND_EVENTS.setLooperLengthBars, { bars: Number(e.target.value) })}
                title="Fixed capture length — the capture stops itself after this many bars (4 beats each). Free stops when you stop."
              >
                <option value="0">Free</option>
                <option value="1">1 bar</option>
                <option value="2">2 bars</option>
                <option value="4">4 bars</option>
                <option value="8">8 bars</option>
              </select>
            </label>
          </div>

          <div className="looper-setting looper-setting--toggles">
            <label className="looper-loop-switch">
              <input type="checkbox" checked={looping} onChange={(e) => emit(FRONTEND_EVENTS.setLooperLooping, { enabled: e.target.checked })} />
              <span className="looper-switch" />
              <span>{looping ? "Loop" : "One shot"}</span>
            </label>

            <label
              className={`looper-loop-switch ${!looping ? "is-disabled" : ""}`}
              title={looping
                ? (overdub ? "Overdub on — record layers over the loop" : "Overdub off — record replaces the loop. Turn on to layer.")
                : "Overdub needs loop mode"}
            >
              <input
                type="checkbox"
                checked={overdub}
                disabled={!looping}
                onChange={(e) => onOverdubChange(e.target.checked)}
              />
              <span className="looper-switch" />
              <span>Overdub</span>
            </label>
          </div>

          <div className="looper-setting looper-setting--crop">
            <Stepper
              label="Crop start"
              value={cropStartBeats}
              min={0}
              max={!recording && totalBeats > 0 ? Math.max(0, totalBeats - 1 - cropEndBeats) : 0}
              onChange={(beats) => emitCrop(beats, cropEndBeats)}
              title="Beats to trim off the start of the loop (4 beats per bar)"
            />

            <Stepper
              label="Crop end"
              value={cropEndBeats}
              min={0}
              max={!recording && totalBeats > 0 ? Math.max(0, totalBeats - 1 - cropStartBeats) : 0}
              onChange={(beats) => emitCrop(cropStartBeats, beats)}
              title="Beats to trim off the end of the loop (4 beats per bar)"
            />
          </div>
        </div>
      </div>
    </section>
  );
}
