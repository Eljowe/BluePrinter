import React from "react";
import iconUrl from "../icon.svg";

// Startup overlay, drawn from the app's editorial-light system (paper,
// ink, cobalt accent, mono readouts). A letterhead: masthead on a
// hairline rule, centered stage with real restore data, and an "ink
// rule" progress line at the bottom that fills as plugins load.
// Phases:
//   starting   — no chain snapshot yet (or no pending loads known):
//                indeterminate ink sweep, "Warming up the signal path"
//   restoring  — deferred VST3 restore in flight: determinate ink fill,
//                live plugin counts + per-chain chips
//   done       — restore finished: full ink rule, "Ready to record"
//   error      — restoreError non-empty: warn-coloured readout
//   leaving    — fade-out to the app
export function SplashScreen({
  visible,
  leaving,
  restoring,
  snapshotReceived,
  progress,
  remaining,
  total,
  chains,
  error,
}) {
  if (!visible) return null;

  const restoringWithProgress = restoring && total > 0;
  const phaseClass = leaving
    ? "is-leaving"
    : restoringWithProgress
      ? "is-restoring"
      : snapshotReceived
        ? "is-done"
        : "is-starting";

  const headline = restoringWithProgress
    ? `Restoring ${total} plugin${total === 1 ? "" : "s"}`
    : snapshotReceived
      ? "Ready to record"
      : "Warming up the signal path";

  const inkStyle = restoringWithProgress ? { width: `${Math.round(progress)}%` } : undefined;

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
            {error ? error : restoringWithProgress ? `${total - remaining} of ${total} plugins ready` : "starting up"}
          </p>

          <p className="bp-splash__tagline">Record a take, name it, note what to work on.</p>

          {restoringWithProgress && chains.length > 0 && (
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
            <span>{restoringWithProgress ? `${remaining} remaining` : "ready"}</span>
            <span>Loading plugins...</span>
          </div>
        </footer>
      </div>
    </div>
  );
}
