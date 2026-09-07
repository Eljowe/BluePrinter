import React, { useEffect, useState } from "react";
import iconUrl from "../icon.svg";

// Startup overlay, drawn from the app's brutalist-light system (paper,
// ink, cobalt accent, mono readouts). A masthead on a 3px rule, a huge
// display-type stage with real restore data, and a thick bordered
// progress bar at the bottom that fills in hard steps as plugins load.
// Phases:
//   starting   — no chain snapshot yet (or no pending loads known):
//                indeterminate ink sweep, "Warming up the signal path"
//   restoring  — deferred VST3 restore in flight: determinate ink fill,
//                live plugin counts + per-chain chips, "≈Ns left" ETA
//                extrapolated from serial-slot throughput
//   finishing  — restore tail: every slot loaded, the last one's saved
//                state applying. C++ blocks inside the plugin (no
//                progress source), so the ink stays full, a spinner runs
//                and a renderer-side clock ticks the real elapsed time.
//   done       — restore finished: full ink rule, "Ready to record"
//   error      — restoreError non-empty: warn-coloured readout
//   leaving    — fade-out to the app
function formatSeconds(totalSec) {
  const s = Math.max(0, Math.floor(totalSec));
  if (s < 60) return `${s}s`;
  const m = Math.floor(s / 60);
  const r = s % 60;
  return r > 0 ? `${m}m ${r}s` : `${m}m`;
}

export function SplashScreen({
  visible,
  leaving,
  restoring,
  snapshotReceived,
  progress,
  remaining,
  total,
  etaSec,
  chains,
  error,
}) {
  const [elapsedSec, setElapsedSec] = useState(0);

  // Tail of the restore: each slot is loaded and the last one's state is
  // being applied. While C++ is inside the plugin's setStateInformation
  // no snapshot events arrive (the message loop is blocked), but JS
  // timers keep running in WebView2 — so this clock is the only honest
  // feedback and it keeps ticking.
  const inTail = restoring && total > 0 && remaining === 0;
  useEffect(() => {
    if (!inTail) {
      setElapsedSec(0);
      return undefined;
    }
    setElapsedSec(0);
    const id = setInterval(() => setElapsedSec((s) => s + 1), 1000);
    return () => clearInterval(id);
  }, [inTail]);

  if (!visible) return null;

  const restoringWithProgress = restoring && total > 0;
  const phaseClass = leaving
    ? "is-leaving"
    : inTail
      ? "is-restoring is-finishing"
      : restoringWithProgress
        ? "is-restoring"
        : snapshotReceived
          ? "is-done"
          : "is-starting";

  const headline = inTail
    ? "Applying saved state"
    : restoringWithProgress
      ? `Restoring ${total} plugin${total === 1 ? "" : "s"}`
      : snapshotReceived
        ? "Ready to record"
        : "Warming up the signal path";

  const status = error
    ? error
    : inTail
      ? `${total} of ${total} loaded — big presets can take a minute or two`
      : restoringWithProgress
        ? `${total - remaining} of ${total} plugins ready${etaSec > 0 ? ` · ≈ ${formatSeconds(etaSec)} left` : ""}`
        : "starting up";

  const inkStyle = restoringWithProgress
    ? { width: `${inTail ? 100 : Math.round(progress)}%` }
    : undefined;

  return (
    <div className={`bp-splash ${phaseClass}`} role="status" aria-live="polite" aria-label="Loading BluePrinter">
      <div className="bp-splash__paper">
        <header className="bp-splash__masthead">
          <img src={iconUrl} alt="" className="bp-splash__emblem" draggable="false" />
          <span className="bp-splash__wordmark">BluePrinter</span>
        </header>

        <div className="bp-splash__stage">
          <h2 className="bp-splash__headline">{headline}</h2>

          <p className={error ? "bp-splash__status is-error" : "bp-splash__status"}>
            {status}
          </p>

          <p className="bp-splash__tagline">Record a take, name it, note what to work on.</p>

          {restoringWithProgress && !inTail && chains.length > 0 && (
            <div className="bp-splash__chips">
              {chains.map((chain) => (
                <span key={chain.id} className="bp-splash__chip">
                  {chain.name} · {chain.pending} left
                </span>
              ))}
            </div>
          )}
        </div>

        <footer className="bp-splash__footer">
          <div className="bp-splash__rule" aria-hidden="true">
            <div className="bp-splash__ink" style={inkStyle} />
          </div>
          <div className="bp-splash__meta">
            <span className="bp-splash__meta-left">
              {inTail && <span className="bp-splash__spinner" aria-hidden="true" />}
              <span>{inTail ? formatSeconds(elapsedSec) : restoringWithProgress ? `${remaining} remaining` : "ready"}</span>
            </span>
            <span>{inTail ? "applying saved state" : "Loading plugins..."}</span>
          </div>
        </footer>
      </div>
    </div>
  );
}