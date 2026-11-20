import { useCallback, useEffect, useState } from "react";
import {
  BACKEND_EVENTS,
  FRONTEND_EVENTS,
  emit,
  getInitialData,
  subscribe,
} from "../bridge";

// Unscaled shell cluster (0060): interface zoom and (standalone only) the
// window maximise toggle. It lives outside the transformed .ui-content so
// the controls stay reachable even at 50% zoom; UiScale owns the zoom state
// and passes the step/fit callbacks down.
const MAXIMISE_KEY = "F11";

function readInitialWindowState() {
  const transport = getInitialData().transport;
  return {
    standalone: Boolean(transport?.standalone),
    maximised: Boolean(transport?.maximised),
  };
}

export default function UiZoomControl({ effectivePercent, isFit, onStep, onFit }) {
  const [windowState, setWindowState] = useState(readInitialWindowState);

  useEffect(() => {
    const unsubscribe = subscribe(BACKEND_EVENTS.transport, (payload) => {
      if (typeof payload !== "object" || payload == null) return;
      setWindowState((prev) => {
        const standalone = payload.standalone !== undefined
          ? Boolean(payload.standalone)
          : prev.standalone;
        const maximised = payload.maximised !== undefined
          ? Boolean(payload.maximised)
          : prev.maximised;
        if (standalone === prev.standalone && maximised === prev.maximised) return prev;
        return { standalone, maximised };
      });
    });
    return unsubscribe;
  }, []);

  useEffect(() => {
    const onKeyDown = (event) => {
      if (event.key !== MAXIMISE_KEY || !windowState.standalone) return;
      event.preventDefault();
      emit(FRONTEND_EVENTS.setMaximised, { enabled: !windowState.maximised });
    };
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, [windowState.standalone, windowState.maximised]);

  const canZoomOut = effectivePercent > 50;
  const canZoomIn = effectivePercent < 300;

  const toggleMaximise = useCallback(() => {
    emit(FRONTEND_EVENTS.setMaximised, { enabled: !windowState.maximised });
  }, [windowState.maximised]);

  const readoutLabel = isFit
    ? `Fit · ${effectivePercent}%`
    : `${effectivePercent}%`;

  return (
    <div className="ui-shell-controls" role="group" aria-label="Interface view">
      <div className="ui-zoom-control" role="group" aria-label="Interface zoom">
        <button
          type="button"
          onClick={() => onStep(-1)}
          disabled={!canZoomOut}
          aria-label="Zoom out"
          title="Zoom out (Ctrl/Cmd + minus)"
        >
          −
        </button>
        <button
          type="button"
          className={`ui-zoom-readout ${isFit ? "is-fit" : ""}`.trim()}
          onClick={onFit}
          aria-label={
            isFit
              ? `Interface fit to window at ${effectivePercent} percent`
              : `Interface zoom ${effectivePercent} percent. Click to fit the window`
          }
          title={isFit ? "Fitted to the window (Ctrl/Cmd + 0)" : "Fit to the window (Ctrl/Cmd + 0)"}
        >
          {readoutLabel}
        </button>
        <button
          type="button"
          onClick={() => onStep(1)}
          disabled={!canZoomIn}
          aria-label="Zoom in"
          title="Zoom in (Ctrl/Cmd + plus)"
        >
          +
        </button>
      </div>

      {windowState.standalone ? (
        <button
          type="button"
          className="ui-maximise"
          onClick={toggleMaximise}
          aria-pressed={windowState.maximised}
          title="Fill the screen with the largest aspect-correct window (F11)"
        >
          {windowState.maximised ? "Restore" : "Maximise"}
        </button>
      ) : null}
    </div>
  );
}
