import { useEffect, useState } from "react";
import { CHAIN_IDS, FRONTEND_EVENTS, emit } from "../bridge";

function basename(path) {
  if (!path) return "";
  const parts = String(path).split(/[\\/]/);
  return parts[parts.length - 1] || String(path);
}

// One row in the chain list. Shared between the MIDI and the audio
// panel — the visual difference is just the "MIDI"/"FX" prefix on
// the open-editor window title (which the backend sets, not us).
function ChainSlotRow({ chain, slot, index, openEditors, onBypassToggle, onRemove, onOpenEditor, onCloseEditor, onMove }) {
  const editorOpen = openEditors.some((e) => e && e.chain === chain && e.index === index);
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
        onMove(from, index);
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
        onClick={() => onMove(index, index - 1)}
        disabled={index === 0}
        title="Move this plugin earlier in the chain"
        aria-label="Move up"
      >
        ↑
      </button>
      <button
        type="button"
        className="fx-button fx-button-small fx-button-icon"
        onClick={() => onMove(index, index + 1)}
        disabled={false /* bounds are checked inside onMove with the real slot length */}
        title="Move this plugin later in the chain"
        aria-label="Move down"
      >
        ↓
      </button>
      <button
        type="button"
        className={`fx-toggle ${slot.bypassed ? "is-off" : "is-on"}`}
        onClick={() => onBypassToggle(index, slot.bypassed)}
        title={slot.bypassed ? "Bypassed — click to enable" : "Enabled — click to bypass"}
        aria-pressed={!slot.bypassed}
      >
        {slot.bypassed ? "Bypassed" : "On"}
      </button>
      {editorOpen ? (
        <button
          type="button"
          className="fx-button fx-button-small"
          onClick={() => onCloseEditor(index)}
          title="Close the plugin's native editor window"
        >
          Close
        </button>
      ) : (
        <button
          type="button"
          className="fx-button fx-button-small"
          onClick={() => onOpenEditor(index)}
          title="Open the plugin's native editor in a separate window"
        >
          Edit
        </button>
      )}
      <button
        type="button"
        className="fx-button fx-button-small fx-button-danger"
        onClick={() => onRemove(index)}
        title="Remove this plugin from the chain"
      >
        Remove
      </button>
    </li>
  );
}

// One of the two chain panels. The "chain" prop is the chain id
// (CHAIN_IDS.midi or CHAIN_IDS.audio); the parent passes a closure
// factory so every action carries the chain id automatically.
function ChainPanel({
  chain,
  title,
  subtitle,
  slots,
  available,
  openEditors,
  scanning,
  onAdd,
  onPickFile,
  onScanFolder,
  onBypassToggle,
  onRemove,
  onOpenEditor,
  onCloseEditor,
  onMove,
}) {
  const [showAvailable, setShowAvailable] = useState(false);

  return (
    <div className="fx-chain-panel" data-chain={chain}>
      <header className="fx-chain-header">
        <div className="fx-chain-title">
          <h3>{title}</h3>
          <p className="fx-chain-subtitle">{subtitle}</p>
        </div>
        <div className="fx-chain-actions">
          <button
            type="button"
            className="fx-button"
            onClick={onPickFile}
            title="Pick a .vst3 file to add"
            disabled={scanning}
          >
            + Add plugin…
          </button>
        </div>
      </header>

      {slots.length === 0 ? (
        <p className="fx-chain-empty">No plugins loaded. The signal passes through dry.</p>
      ) : (
        <ol className="fx-chain-slots">
          {slots.map((slot, index) => (
            <ChainSlotRow
              key={`${chain}-${slot.path || slot.name || "slot"}-${index}`}
              chain={chain}
              slot={slot}
              index={index}
              openEditors={openEditors}
              onBypassToggle={onBypassToggle}
              onRemove={onRemove}
              onOpenEditor={onOpenEditor}
              onCloseEditor={onCloseEditor}
              onMove={onMove}
            />
          ))}
        </ol>
      )}

      {available.length > 0 ? (
        <div className="fx-available">
          <button
            type="button"
            className="fx-available-toggle"
            onClick={() => setShowAvailable((v) => !v)}
          >
            {showAvailable ? "▾" : "▸"} Available plugins ({available.length})
          </button>
          {showAvailable ? (
            <ul className="fx-available-list">
              {available.map((p, i) => (
                <li key={`${chain}-${p.path || p.name}-${i}`}>
                  <button
                    type="button"
                    className="fx-available-item"
                    onClick={() => onAdd(p.path)}
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
    </div>
  );
}

export function PluginChain({ chainState, availablePlugins, defaultFolder, scanState }) {
  // The backend pushes a single snapshot that holds both chains. The
  // shape is:
  //   {
  //     folder: "C:\\Program Files\\Common Files\\VST3",
  //     midiChain:  { slots: [...] },
  //     audioChain: { slots: [...] },
  //     plugins:    [...],          // the folder scan result
  //     blocklist:  ["...\\Foo.vst3"],
  //     openEditors: [{ chain, index }, ...],
  //     restoreError: "..."
  //   }
  // Each chain's `slots` array is the same shape PluginChain.jsx used
  // pre-split, so the row component is identical.
  const midiSlots  = Array.isArray(chainState?.midiChain?.slots)  ? chainState.midiChain.slots  : [];
  const audioSlots = Array.isArray(chainState?.audioChain?.slots) ? chainState.audioChain.slots : [];
  const available = Array.isArray(availablePlugins) ? availablePlugins : [];
  const openEditors = Array.isArray(chainState?.openEditors) ? chainState.openEditors : [];

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
  // a chain.
  useEffect(() => {
    emit(FRONTEND_EVENTS.getVst3Chain);
  }, []);

  // Per-action factories: each closure captures the chain id so the
  // caller doesn't have to remember it on every event. The backend
  // defaults to "audioChain" if the field is absent, but we always
  // pass it explicitly to keep the intent clear.

  const makeAddHandlers = (chain) => ({
    onPickFile: () => emit(FRONTEND_EVENTS.addVst3, { chain }),
    onAdd: (path) => { if (path) emit(FRONTEND_EVENTS.addVst3, { chain, path }); },
  });

  const makeBypassHandler = (chain) => (index, currentBypassed) =>
    emit(FRONTEND_EVENTS.setVst3Bypass, { chain, index, bypassed: !currentBypassed });

  const makeRemoveHandler = (chain) => (index) => {
    if (window.confirm(`Remove this plugin from the ${chain === CHAIN_IDS.midi ? "MIDI" : "FX"} chain?`)) {
      emit(FRONTEND_EVENTS.removeVst3, { chain, index });
    }
  };

  const makeOpenEditorHandler = (chain) => (index) =>
    emit(FRONTEND_EVENTS.openVst3Editor, { chain, index });

  const makeCloseEditorHandler = (chain) => (index) =>
    emit(FRONTEND_EVENTS.closeVst3Editor, { chain, index });

  const makeMoveHandler = (chain) => (from, to) => {
    if (from === to) return;
    const limit = chain === CHAIN_IDS.midi ? midiSlots.length : audioSlots.length;
    if (to < 0 || to >= limit) return;
    emit(FRONTEND_EVENTS.moveVst3, { chain, from, to });
  };

  const midiAdd  = makeAddHandlers(CHAIN_IDS.midi);
  const audioAdd = makeAddHandlers(CHAIN_IDS.audio);

  const handleScanFolder = () => emit(FRONTEND_EVENTS.scanVst3Folder, {});

  return (
    <section className="fx-chain">
      <header className="fx-chain-section-header">
        <div className="fx-chain-title">
          <h2>Plugin chains</h2>
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
            onClick={handleScanFolder}
            title="Scan the default VST3 folder for installed plugins"
            disabled={scanning}
          >
            {scanning ? "Scanning…" : "Scan VST3 folder"}
          </button>
        </div>
      </header>

      <p className="fx-chain-help">
        The MIDI chain runs first on your keyboard input (arpeggiators, chord generators,
        instruments). The audio chain runs second on the combined signal (amp sims, EQ,
        reverb). Plugins in either chain can read the MIDI buffer, so note-aware audio
        plugins still see what you're playing.
      </p>

      <ChainPanel
        chain={CHAIN_IDS.midi}
        title="MIDI chain"
        subtitle="Runs first — good for arpeggiators, chord tools, and instruments."
        slots={midiSlots}
        available={available}
        openEditors={openEditors}
        scanning={scanning}
        onPickFile={midiAdd.onPickFile}
        onAdd={midiAdd.onAdd}
        onScanFolder={handleScanFolder}
        onBypassToggle={makeBypassHandler(CHAIN_IDS.midi)}
        onRemove={makeRemoveHandler(CHAIN_IDS.midi)}
        onOpenEditor={makeOpenEditorHandler(CHAIN_IDS.midi)}
        onCloseEditor={makeCloseEditorHandler(CHAIN_IDS.midi)}
        onMove={makeMoveHandler(CHAIN_IDS.midi)}
      />

      <ChainPanel
        chain={CHAIN_IDS.audio}
        title="Audio FX chain"
        subtitle="Runs second on the combined signal — good for amp sims, EQ, and reverb."
        slots={audioSlots}
        available={available}
        openEditors={openEditors}
        scanning={scanning}
        onPickFile={audioAdd.onPickFile}
        onAdd={audioAdd.onAdd}
        onScanFolder={handleScanFolder}
        onBypassToggle={makeBypassHandler(CHAIN_IDS.audio)}
        onRemove={makeRemoveHandler(CHAIN_IDS.audio)}
        onOpenEditor={makeOpenEditorHandler(CHAIN_IDS.audio)}
        onCloseEditor={makeCloseEditorHandler(CHAIN_IDS.audio)}
        onMove={makeMoveHandler(CHAIN_IDS.audio)}
      />
    </section>
  );
}
