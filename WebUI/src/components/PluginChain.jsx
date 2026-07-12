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
  // The backend pushes a list of slot indices whose native editor
  // windows are currently open, so the row can show "Close" instead
  // of "Edit" and let the user tear the window down from here.
  const openEditors = Array.isArray(chainState?.openEditors) ? chainState.openEditors : [];
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

  // Reorder: emit a single move request and let the backend swap the
  // slots in place. Both the ↑/↓ buttons and drag-and-drop funnel
  // through here so the two input methods stay consistent.
  const handleMove = (from, to) => {
    if (from === to) return;
    if (to < 0 || to >= slots.length) return;
    emit(FRONTEND_EVENTS.moveVst3, { from, to });
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
            title="Scan the default VST3 folder for installed plugins"
            disabled={scanning}
          >
            {scanning ? "Scanning…" : "Scan VST3 folder"}
          </button>
        </div>
      </header>

      {slots.length === 0 ? (
        <p className="fx-chain-empty">No plugins loaded. The signal passes through dry.</p>
      ) : (
        <ol className="fx-chain-slots">
          {slots.map((slot, index) => {
            const editorOpen = openEditors.includes(index);
            return (
              <li
                key={`${slot.path || slot.name || "slot"}-${index}`}
                className={`fx-slot ${slot.bypassed ? "is-bypassed" : ""}`}
                draggable
                onDragStart={(event) => {
                  event.dataTransfer.effectAllowed = "move";
                  event.dataTransfer.setData("text/x-fx-slot", String(index));
                }}
                onDragOver={(event) => {
                  if (event.dataTransfer.types.includes("text/x-fx-slot")) {
                    event.preventDefault();
                    event.dataTransfer.dropEffect = "move";
                    event.currentTarget.classList.add("is-drop-target");
                  }
                }}
                onDragLeave={(event) => {
                  event.currentTarget.classList.remove("is-drop-target");
                }}
                onDrop={(event) => {
                  event.currentTarget.classList.remove("is-drop-target");
                  const raw = event.dataTransfer.getData("text/x-fx-slot");
                  if (raw === "") return;
                  const from = Number.parseInt(raw, 10);
                  if (Number.isNaN(from)) return;
                  handleMove(from, index);
                }}
                onDragEnd={(event) => {
                  event.currentTarget.classList.remove("is-drop-target");
                }}
              >
                <span className="fx-slot-handle" title="Drag to reorder" aria-hidden="true">⋮⋮</span>
                <span className="fx-slot-index">{index + 1}</span>
                <div className="fx-slot-info">
                  <span className="fx-slot-name" title={slot.path || ""}>
                    {slot.name || basename(slot.path) || "Unknown plugin"}
                  </span>
                  <span className="fx-slot-path">{basename(slot.path)}</span>
                </div>
                <button
                  type="button"
                  className="fx-button fx-button-small fx-button-icon"
                  onClick={() => handleMove(index, index - 1)}
                  disabled={index === 0}
                  title="Move this plugin earlier in the chain"
                  aria-label="Move up"
                >
                  ↑
                </button>
                <button
                  type="button"
                  className="fx-button fx-button-small fx-button-icon"
                  onClick={() => handleMove(index, index + 1)}
                  disabled={index === slots.length - 1}
                  title="Move this plugin later in the chain"
                  aria-label="Move down"
                >
                  ↓
                </button>
                <button
                  type="button"
                  className={`fx-toggle ${slot.bypassed ? "is-off" : "is-on"}`}
                  onClick={() => handleBypassToggle(index, slot.bypassed)}
                  title={slot.bypassed ? "Bypassed — click to enable" : "Enabled — click to bypass"}
                  aria-pressed={!slot.bypassed}
                >
                  {slot.bypassed ? "Bypassed" : "On"}
                </button>
                {editorOpen ? (
                  <button
                    type="button"
                    className="fx-button fx-button-small"
                    onClick={() => handleCloseEditor(index)}
                    title="Close the plugin's native editor window"
                  >
                    Close
                  </button>
                ) : (
                  <button
                    type="button"
                    className="fx-button fx-button-small"
                    onClick={() => handleOpenEditor(index)}
                    title="Open the plugin's native editor in a separate window"
                  >
                    Edit
                  </button>
                )}
                <button
                  type="button"
                  className="fx-button fx-button-small fx-button-danger"
                  onClick={() => handleRemove(index)}
                  title="Remove this plugin from the chain"
                >
                  Remove
                </button>
              </li>
            );
          })}
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
