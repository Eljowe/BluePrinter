import { useEffect, useState } from "react";
import { BACKEND_EVENTS, FRONTEND_EVENTS, emit, subscribe } from "../bridge";

function basename(path) {
  if (!path) return "";
  const parts = String(path).split(/[\\/]/);
  return parts[parts.length - 1] || String(path);
}

export function PluginChain({ chainState, availablePlugins, defaultFolder, scanState }) {
  const slots = Array.isArray(chainState?.slots) ? chainState.slots : [];
  const available = Array.isArray(availablePlugins) ? availablePlugins : [];
  const [showAdd, setShowAdd] = useState(false);

  const scanning = Boolean(scanState?.active);
  const scanTotal = Number(scanState?.total ?? 0);
  const scanCurrent = Number(scanState?.current ?? 0);
  const scanFile = typeof scanState?.currentFile === "string" ? scanState.currentFile : "";
  const folderLabel = scanning
    ? scanTotal > 0
      ? `Scanning ${scanCurrent} / ${scanTotal}${scanFile ? ` — ${scanFile}` : ""}`
      : "Scanning…"
    : defaultFolder
      ? `Scanning: ${defaultFolder}`
      : "";

  // Ask the backend for the current chain snapshot once the WebView has
  // finished loading. Without this the UI is empty until the user mutates
  // the chain.
  useEffect(() => {
    emit(FRONTEND_EVENTS.getVst3Chain);
  }, []);

  const handleBypassToggle = (index, currentBypassed) => {
    emit(FRONTEND_EVENTS.setVst3Bypass, { index, bypassed: !currentBypassed });
  };

  const handleRemove = (index) => {
    if (window.confirm(`Remove ${slots[index]?.name || "this plugin"} from the chain?`)) {
      emit(FRONTEND_EVENTS.removeVst3, { index });
    }
  };

  const handleOpenEditor = (index) => {
    emit(FRONTEND_EVENTS.openVst3Editor, { index });
  };

  const handleCloseEditor = (index) => {
    emit(FRONTEND_EVENTS.closeVst3Editor, { index });
  };

  const handleAdd = (path) => {
    if (!path) return;
    setShowAdd(false);
    emit(FRONTEND_EVENTS.addVst3, { path });
  };

  const handlePickFile = () => {
    setShowAdd(false);
    emit(FRONTEND_EVENTS.addVst3, {});
  };

  const handleScanFolder = () => {
    emit(FRONTEND_EVENTS.scanVst3Folder, {});
  };

  return (
    <section className="fx-chain">
      <header className="fx-chain-header">
        <div className="fx-chain-title">
          <h2>FX chain</h2>
          <p
            className={`fx-chain-folder ${scanning ? "is-scanning" : ""}`}
            title={scanning ? scanState?.folder || defaultFolder || "" : defaultFolder || ""}
          >
            {folderLabel}
          </p>
        </div>
        <div className="fx-chain-actions">
          <button
            type="button"
            className="fx-button"
            onClick={handlePickFile}
            title="Pick a .vst3 file to add"
            disabled={scanning}
          >
            + Add plugin…
          </button>
          <button
            type="button"
            className="fx-button"
            onClick={handleScanFolder}
            title="Pick a folder and scan it for VST3 plugins"
            disabled={scanning}
          >
            {scanning ? "Scanning…" : "Scan folder…"}
          </button>
        </div>
      </header>

      {slots.length === 0 ? (
        <p className="fx-chain-empty">No plugins loaded. The signal passes through dry.</p>
      ) : (
        <ol className="fx-chain-slots">
          {slots.map((slot, index) => (
            <li
              key={`${slot.path || slot.name || "slot"}-${index}`}
              className={`fx-slot ${slot.bypassed ? "is-bypassed" : ""}`}
            >
              <span className="fx-slot-index">{index + 1}</span>
              <div className="fx-slot-info">
                <span className="fx-slot-name" title={slot.path || ""}>
                  {slot.name || basename(slot.path) || "Unknown plugin"}
                </span>
                <span className="fx-slot-path">{basename(slot.path)}</span>
              </div>
              <button
                type="button"
                className={`fx-toggle ${slot.bypassed ? "is-off" : "is-on"}`}
                onClick={() => handleBypassToggle(index, slot.bypassed)}
                title={slot.bypassed ? "Bypassed — click to enable" : "Enabled — click to bypass"}
                aria-pressed={!slot.bypassed}
              >
                {slot.bypassed ? "Bypassed" : "On"}
              </button>
              <button
                type="button"
                className="fx-button fx-button-small"
                onClick={() => handleOpenEditor(index)}
                title="Open the plugin's native editor in a separate window"
              >
                Edit
              </button>
              <button
                type="button"
                className="fx-button fx-button-small fx-button-danger"
                onClick={() => handleRemove(index)}
                title="Remove this plugin from the chain"
              >
                Remove
              </button>
            </li>
          ))}
        </ol>
      )}

      {available.length > 0 ? (
        <div className="fx-available">
          <button
            type="button"
            className="fx-available-toggle"
            onClick={() => setShowAdd((v) => !v)}
          >
            {showAdd ? "▾" : "▸"} Available plugins ({available.length})
          </button>
          {showAdd ? (
            <ul className="fx-available-list">
              {available.map((p, i) => (
                <li key={`${p.path || p.name}-${i}`}>
                  <button
                    type="button"
                    className="fx-available-item"
                    onClick={() => handleAdd(p.path)}
                    title={p.path || ""}
                  >
                    <span className="fx-available-name">{p.name || basename(p.path)}</span>
                    {p.manufacturer ? (
                      <span className="fx-available-meta">{p.manufacturer}</span>
                    ) : null}
                  </button>
                </li>
              ))}
            </ul>
          ) : null}
        </div>
      ) : null}
    </section>
  );
}
