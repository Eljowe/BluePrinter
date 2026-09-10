import { useEffect, useState } from "react";
import { FRONTEND_EVENTS, emit } from "../bridge";
import { LevelMeter } from "./LevelMeter";
import { Knob } from "./controls";
import {
  IconArrowDown,
  IconArrowUp,
  IconChevronDown,
  IconChevronRight,
  IconEdit,
  IconGrip,
  IconPlus,
  IconScan,
  IconTrash,
  IconX,
} from "./icons";

function basename(path) {
  if (!path) return "";
  const parts = String(path).split(/[\\/]/);
  return parts[parts.length - 1] || String(path);
}

function sortedNumbers(values) {
  return [...new Set(values.filter((v) => Number.isFinite(Number(v))).map(Number))].sort((a, b) => a - b);
}

const ALL_MIDI_CHANNELS = Array.from({ length: 16 }, (_, i) => i + 1);

// One row in the chain list. Rendered for every chain panel.
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
      <span className="fx-slot-handle" title="Drag to reorder" aria-hidden="true">
        <IconGrip size={14} />
      </span>
      <span className="fx-slot-index">{index + 1}</span>
      <div className="fx-slot-info">
        <span className="fx-slot-name" title={slot.path || ""}>
          {slot.name || basename(slot.path) || "Unknown plugin"}
        </span>
        <span className="fx-slot-path">{basename(slot.path)}</span>
      </div>
      <div className="fx-slot-controls">
        <button
          type="button"
          className="icon-btn"
          onClick={() => onMove(index, index - 1)}
          disabled={index === 0}
          title="Move this plugin earlier in the chain"
          aria-label="Move up"
        >
          <IconArrowUp size={13} />
        </button>
        <button
          type="button"
          className="icon-btn"
          onClick={() => onMove(index, index + 1)}
          disabled={false /* bounds are checked inside onMove with the real slot length */}
          title="Move this plugin later in the chain"
          aria-label="Move down"
        >
          <IconArrowDown size={13} />
        </button>
        <button
          type="button"
          className={`fx-power ${slot.bypassed ? "is-off" : "is-on"}`}
          onClick={() => onBypassToggle(index, slot.bypassed)}
          title={slot.bypassed ? "Bypassed — click to enable" : "Enabled — click to bypass"}
          aria-pressed={!slot.bypassed}
        >
          {slot.bypassed ? "Off" : "On"}
        </button>
        <button
          type="button"
          className={`icon-btn ${editorOpen ? "is-active" : ""}`}
          onClick={() => (editorOpen ? onCloseEditor(index) : onOpenEditor(index))}
          title={editorOpen
            ? "Close the plugin's native editor window"
            : "Open the plugin's native editor in a separate window"}
          aria-label={editorOpen ? "Close editor" : "Open editor"}
          aria-pressed={editorOpen}
        >
          <IconEdit size={13} />
        </button>
        <button
          type="button"
          className="icon-btn icon-btn-danger"
          onClick={() => onRemove(index)}
          title="Remove this plugin from the chain"
          aria-label="Remove plugin"
        >
          <IconX size={13} />
        </button>
      </div>
    </li>
  );
}

// One chain panel. The "chain" prop is the stable chain id; the parent
// passes closure factories so every action carries the chain id.
function ChainPanel({
  chain,
  name,
  inputs,
  wantsMidi,
  recordOnCapture,
  volume,
  muted,
  monitorSolo,
  monitorMuted,
  midiChannels,
  slots,
  pending,
  available,
  openEditors,
  scanning,
  inputChannels,
  levels,
  onAdd,
  onPickFile,
  onBypassToggle,
  onRemove,
  onOpenEditor,
  onCloseEditor,
  onMove,
  onRemoveChain,
}) {
  const [showAvailable, setShowAvailable] = useState(false);
  const [showMidiChannels, setShowMidiChannels] = useState(false);
  const [nameDraft, setNameDraft] = useState(name);
  // Optimistic drafts for the continuous/high-frequency controls. The
  // backend does not round-trip a chain snapshot for volume/mute (that
  // snapshot serializes every plugin's state, which made the knob
  // laggy), so these mirrors are the source of truth between backend
  // pushes.
  const [volumeDraft, setVolumeDraft] = useState(volume);
  const [mutedDraft, setMutedDraft] = useState(muted);
  const [soloDraft, setSoloDraft] = useState(monitorSolo);
  const [monitorMutedDraft, setMonitorMutedDraft] = useState(monitorMuted);

  // Keep the drafts in sync when the backend pushes a snapshot, unless
  // the user is actively interacting.
  useEffect(() => {
    setNameDraft(name);
  }, [name]);
  useEffect(() => {
    setVolumeDraft(volume);
  }, [volume]);
  useEffect(() => {
    setMutedDraft(muted);
  }, [muted]);
  useEffect(() => {
    setSoloDraft(monitorSolo);
  }, [monitorSolo]);
  useEffect(() => {
    setMonitorMutedDraft(monitorMuted);
  }, [monitorMuted]);

  const toggleMidi = (enabled) => emit(FRONTEND_EVENTS.setVst3MidiPass, { chain, enabled });
  const toggleRecord = (enabled) => emit(FRONTEND_EVENTS.setChainRecord, { chain, enabled });
  const toggleMute = (nextMuted) => {
    setMutedDraft(nextMuted);
    emit(FRONTEND_EVENTS.setChainMute, { chain, muted: nextMuted });
  };
  const changeVolume = (next) => {
    setVolumeDraft(next);
    emit(FRONTEND_EVENTS.setChainVolume, { chain, volume: next });
  };
  const toggleSolo = (nextSolo) => {
    setSoloDraft(nextSolo);
    emit(FRONTEND_EVENTS.setChainMonitorSolo, { chain, solo: nextSolo });
  };
  const toggleMonitorMute = (nextMuted) => {
    setMonitorMutedDraft(nextMuted);
    emit(FRONTEND_EVENTS.setChainMonitorMute, { chain, muted: nextMuted });
  };

  const toggleInput = (ch) => {
    const set = new Set(inputs);
    if (set.has(ch)) set.delete(ch);
    else set.add(ch);
    emit(FRONTEND_EVENTS.setChainInputs, { chain, inputs: sortedNumbers([...set]) });
  };

  const toggleMidiChannel = (ch) => {
    const set = new Set(midiChannels);
    if (set.has(ch)) set.delete(ch);
    else set.add(ch);
    emit(FRONTEND_EVENTS.setChainMidiChannels, { chain, channels: sortedNumbers([...set]) });
  };

  const setAllMidiChannels = (on) => {
    emit(FRONTEND_EVENTS.setChainMidiChannels, {
      chain,
      channels: on ? ALL_MIDI_CHANNELS : [],
    });
  };

  const commitName = () => {
    const trimmed = nameDraft.trim();
    if (trimmed && trimmed !== name) emit(FRONTEND_EVENTS.renameChain, { chain, name: trimmed });
  };

  const channelCount = Math.min(Math.max(1, Number(inputChannels) || 1), 8);
  const midiCount = midiChannels.length;

  const inputLabel = inputs.length === 0
    ? "no audio in"
    : `in ${sortedNumbers(inputs).map((i) => i + 1).join("+")}`;
  const midiLabel = !wantsMidi ? "no MIDI"
    : midiCount >= 16 ? "all MIDI"
    : midiCount === 0 ? "MIDI none"
    : `MIDI ${sortedNumbers(midiChannels).join(",")}`;
  const totalPlugins = slots.length + pending;
  const pluginLabel = pending > 0
    ? `${totalPlugins} plugin${totalPlugins === 1 ? "" : "s"} restoring…`
    : slots.length === 0 ? "no plugins"
    : `${slots.length} plugin${slots.length === 1 ? "" : "s"}`;
  return (
    <div className="fx-chain-panel" data-chain={chain}>
      <header className="fx-chain-header">
        <input
          type="text"
          className="fx-chain-name"
          value={nameDraft}
          onChange={(e) => setNameDraft(e.target.value)}
          onBlur={commitName}
          onKeyDown={(e) => {
            if (e.key === "Enter") e.currentTarget.blur();
            if (e.key === "Escape") setNameDraft(name);
          }}
          title="Chain name"
          aria-label="Chain name"
        />
        <label
          className={`fx-midi-toggle ${recordOnCapture ? "is-on" : ""}`}
          title="Include this chain's output in take and loop captures"
        >
          <input type="checkbox" checked={recordOnCapture} onChange={(e) => toggleRecord(e.target.checked)} />
          <span className="fx-midi-toggle-box" aria-hidden="true" />
          Record
        </label>
        <label
          className={`fx-midi-toggle ${wantsMidi ? "is-on" : ""}`}
          title="Pass the MIDI buffer to this chain's plugins (note-aware amp sims, synths, arpeggiators)"
        >
          <input type="checkbox" checked={wantsMidi} onChange={(e) => toggleMidi(e.target.checked)} />
          <span className="fx-midi-toggle-box" aria-hidden="true" />
          MIDI
        </label>
        <button
          type="button"
          className={`fx-midi-toggle ${showMidiChannels ? "is-on" : ""}`}
          onClick={() => setShowMidiChannels((v) => !v)}
          title="Which MIDI channels (1–16) this chain listens to"
          aria-expanded={showMidiChannels}
        >
          Ch
        </button>
        <button
          type="button"
          className="btn btn-sm"
          onClick={onPickFile}
          title="Pick a .vst3 file to add"
          disabled={scanning}
        >
          <IconPlus size={13} />
          Add
        </button>
        <button
          type="button"
          className="icon-btn icon-btn-danger"
          onClick={onRemoveChain}
          title="Remove this chain"
          aria-label="Remove chain"
        >
          <IconTrash size={13} />
        </button>
      </header>

      {showMidiChannels ? (
        <div className="fx-midi-channels">
          <div className="fx-midi-channels-actions">
            <span className="fx-midi-channels-label">MIDI channels</span>
            <button type="button" className="fx-midi-channels-all" onClick={() => setAllMidiChannels(true)}>
              All
            </button>
            <button type="button" className="fx-midi-channels-all" onClick={() => setAllMidiChannels(false)}>
              None
            </button>
          </div>
          <div className="fx-midi-channel-grid">
            {ALL_MIDI_CHANNELS.map((ch) => (
              <button
                key={ch}
                type="button"
                className={`fx-midi-channel-chip ${midiChannels.includes(ch) ? "is-on" : ""}`}
                onClick={() => toggleMidiChannel(ch)}
                aria-pressed={midiChannels.includes(ch)}
              >
                {ch}
              </button>
            ))}
          </div>
        </div>
      ) : null}

      <div className="fx-chain-controls">
        <div className="fx-input-checks" title="Which input channels feed this chain">
          <span className="fx-input-checks-label">In</span>
          {Array.from({ length: channelCount }, (_, i) => i).map((ch) => (
            <button
              key={ch}
              type="button"
              className={`fx-input-chip ${inputs.includes(ch) ? "is-on" : ""}`}
              onClick={() => toggleInput(ch)}
              title={`Input channel ${ch + 1}`}
              aria-pressed={inputs.includes(ch)}
            >
              {ch + 1}
            </button>
          ))}
        </div>
        <Knob
          label="Vol"
          min={-60}
          max={12}
          value={volumeDraft}
          onChange={changeVolume}
          step="0.5"
          decimals={1}
          unit="dB"
          className="fx-volume-knob"
        />
        <label
          className={`fx-midi-toggle ${mutedDraft ? "is-on" : ""}`}
          title="Hard mute: the chain is excluded from both the monitor and the capture"
        >
          <input type="checkbox" checked={mutedDraft} onChange={(e) => toggleMute(e.target.checked)} />
          <span className="fx-midi-toggle-box" aria-hidden="true" />
          Mute
        </label>
        <label
          className={`fx-midi-toggle fx-solo-toggle ${soloDraft ? "is-on" : ""}`}
          title="Solo for monitoring: the monitor mix becomes only the soloed chains and the dry input is muted. The capture is unchanged."
        >
          <input type="checkbox" checked={soloDraft} onChange={(e) => toggleSolo(e.target.checked)} />
          <span className="fx-midi-toggle-box" aria-hidden="true" />
          Solo
        </label>
        <label
          className={`fx-midi-toggle fx-monmute-toggle ${monitorMutedDraft ? "is-on" : ""}`}
          title="Monitor mute: this chain is silenced in the monitor only. The capture is unchanged."
        >
          <input type="checkbox" checked={monitorMutedDraft} onChange={(e) => toggleMonitorMute(e.target.checked)} />
          <span className="fx-midi-toggle-box" aria-hidden="true" />
          Mon
        </label>
        <div className="fx-chain-meter">
          <LevelMeter level={levels?.level ?? 0} peak={levels?.peak ?? 0} />
        </div>
      </div>

      <p className="fx-chain-subtitle">
        {inputLabel} · {midiLabel} ·{" "}
        {[
          mutedDraft ? "muted" : null,
          soloDraft ? "solo" : null,
          monitorMutedDraft ? "mon-muted" : null,
          !mutedDraft && !soloDraft && !monitorMutedDraft ? pluginLabel : null,
        ].filter(Boolean).join(" · ")}
      </p>

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
            aria-expanded={showAvailable}
          >
            {showAvailable ? <IconChevronDown size={13} /> : <IconChevronRight size={13} />}
            Available plugins
            <span className="fx-available-count">{available.length}</span>
          </button>
          {showAvailable ? (
            <ul className="fx-available-list">
              {available.map((p, i) => {
                // A plugin already in this chain is disabled: two
                // instances of the same .vst3 in one chain crash some
                // plugins (Neural DSP "X" amp sims). The same plugin is
                // still addable to other chains.
                const alreadyInChain = (slots || []).some((s) => s.path === p.path);
                return (
                  <li key={`${chain}-${p.path || p.name}-${i}`}>
                    <button
                      type="button"
                      className={`fx-available-item${alreadyInChain ? " is-in-chain" : ""}`}
                      onClick={() => onAdd(p.path)}
                      disabled={alreadyInChain}
                      title={alreadyInChain ? "Already in this chain" : p.path || ""}
                    >
                      <span className="fx-available-name">{p.name || basename(p.path)}</span>
                      {alreadyInChain ? (
                        <span className="fx-available-meta">in this chain</span>
                      ) : p.manufacturer ? (
                        <span className="fx-available-meta">{p.manufacturer}</span>
                      ) : null}
                    </button>
                  </li>
                );
              })}
            </ul>
          ) : null}
        </div>
      ) : null}
    </div>
  );
}

export function PluginChain({ chainState, inputChannels, chainLevels, availablePlugins, defaultFolder, scanState }) {
  // The backend pushes a single snapshot. The shape is:
  //   {
  //     folder: "C:\\Program Files\\Common Files\\VST3",
  //     inputChannels: 4,
  //     chains: [
  //       { id, name, inputs: [0,1], wantsMidi, recordOnCapture,
  //         volume, muted, midiChannels: [1..16],
  //         slots: [{ path, bypassed, name }] },
  //       ...
  //     ],
  //     plugins:    [...],          // the folder scan result
  //     blocklist:  ["...\\Foo.vst3"],
  //     openEditors: [{ chain, index }, ...],
  //     restoreError: "..."
  //   }
  // chainLevels (from the 30 Hz transport push) carries the live meters:
  //   [{ chain, level, peak }, ...]
  const chains = Array.isArray(chainState?.chains) ? chainState.chains : [];
  const available = Array.isArray(availablePlugins) ? availablePlugins : [];
  const openEditors = Array.isArray(chainState?.openEditors) ? chainState.openEditors : [];
  const levels = Array.isArray(chainLevels) ? chainLevels : [];

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
  // caller doesn't have to remember it on every event.

  const makeAddHandlers = (chain) => ({
    onPickFile: () => emit(FRONTEND_EVENTS.addVst3, { chain }),
    onAdd: (path) => { if (path) emit(FRONTEND_EVENTS.addVst3, { chain, path }); },
  });

  const makeBypassHandler = (chain) => (index, currentBypassed) =>
    emit(FRONTEND_EVENTS.setVst3Bypass, { chain, index, bypassed: !currentBypassed });

  const makeRemoveHandler = (chain) => (index) =>
    emit(FRONTEND_EVENTS.removeVst3, { chain, index });

  const makeOpenEditorHandler = (chain) => (index) =>
    emit(FRONTEND_EVENTS.openVst3Editor, { chain, index });

  const makeCloseEditorHandler = (chain) => (index) =>
    emit(FRONTEND_EVENTS.closeVst3Editor, { chain, index });

  const makeMoveHandler = (chain) => (slots) => (from, to) => {
    if (from === to) return;
    if (to < 0 || to >= slots.length) return;
    emit(FRONTEND_EVENTS.moveVst3, { chain, from, to });
  };

  const handleAddChain = () => emit(FRONTEND_EVENTS.addChain, {});
  const handleScanFolder = () => emit(FRONTEND_EVENTS.scanVst3Folder, {});

  const handleRemoveChain = (chain) => emit(FRONTEND_EVENTS.removeChain, { chain });

  return (
    <section className="fx-chain">
      <header className="section-header">
        <div className="section-title">
          <span className="section-index" aria-hidden="true">03</span>
          <h2>FX chains</h2>
          <p
            className={`fx-chain-folder ${scanning ? "is-scanning" : ""}`}
            title={scanning ? scanState?.folder || defaultFolder || "" : defaultFolder || ""}
          >
            {folderLabel}
          </p>
        </div>
        <div className="section-actions">
          <button
            type="button"
            className="btn btn-sm"
            onClick={handleScanFolder}
            title="Scan the default VST3 folder for installed plugins"
            disabled={scanning}
          >
            <IconScan size={13} />
            {scanning ? "Scanning…" : "Scan VST3 folder"}
          </button>
          <button
            type="button"
            className="btn btn-sm"
            onClick={handleAddChain}
            title="Add a new parallel chain"
          >
            <IconPlus size={13} />
            Add chain
          </button>
        </div>
      </header>

      <p className="fx-chain-help">
        Chains run in parallel: each one processes only its selected input channels (and the MIDI
        channels it listens to) and its output is mixed over the dry signal with its own volume.
        The Record toggle decides which chains are baked into take and loop captures — leave a
        synth chain out of a guitar take, or record a chain with no audio input for a pure MIDI
        instrument. Chains never hear each other: a guitar amp sim only ever sees your guitar.
      </p>

      {chains.length === 0 ? (
        <p className="fx-chain-empty">No chains yet. Add one to process your signal.</p>
      ) : (
        <div className="fx-chain-panels">
          {chains.map((chain) => (
            <ChainPanel
              key={chain.id}
              chain={chain.id}
              name={chain.name}
              inputs={Array.isArray(chain.inputs) ? chain.inputs : [0, 1]}
              wantsMidi={chain.wantsMidi !== false}
              recordOnCapture={chain.recordOnCapture !== false}
              volume={Number.isFinite(Number(chain.volume)) ? Number(chain.volume) : 0}
              muted={Boolean(chain.muted)}
              monitorSolo={Boolean(chain.monitorSolo)}
              monitorMuted={Boolean(chain.monitorMuted)}
              midiChannels={Array.isArray(chain.midiChannels) ? chain.midiChannels : ALL_MIDI_CHANNELS}
              slots={Array.isArray(chain.slots) ? chain.slots : []}
              pending={Number(chain.pending) || 0}
              available={available}
              openEditors={openEditors}
              scanning={scanning}
              inputChannels={inputChannels}
              levels={levels.find((l) => l && l.chain === chain.id)}
              onPickFile={makeAddHandlers(chain.id).onPickFile}
              onAdd={makeAddHandlers(chain.id).onAdd}
              onBypassToggle={makeBypassHandler(chain.id)}
              onRemove={makeRemoveHandler(chain.id)}
              onOpenEditor={makeOpenEditorHandler(chain.id)}
              onCloseEditor={makeCloseEditorHandler(chain.id)}
              onMove={makeMoveHandler(chain.id)(Array.isArray(chain.slots) ? chain.slots : [])}
              onRemoveChain={() => handleRemoveChain(chain.id)}
            />
          ))}
        </div>
      )}
    </section>
  );
}
