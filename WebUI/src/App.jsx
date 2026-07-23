import { useEffect, useMemo, useState } from "react";
import { Transport } from "./components/Transport";
import { LibraryFolderRow } from "./components/LibraryFolderRow";
import { SnippetList } from "./components/SnippetList";
import { Notification } from "./components/Notification";
import { PluginChain } from "./components/PluginChain";
import { BACKEND_EVENTS, FRONTEND_EVENTS, emit, getInitialData, subscribe } from "./bridge";
import iconUrl from "./icon.svg";

const PARAM_IDS = {
  gain: "Gain",
};

function readInitialParameters() {
  const first = getInitialData().parameters?.[0];
  if (!first) return { gain: 0.7 };
  return { gain: Number(first.gain ?? 0.7) };
}

function readInitialSnippets() {
  const raw = getInitialData().snippets;
  if (Array.isArray(raw)) return raw;
  if (raw && Array.isArray(raw.snippets)) return raw.snippets;
  return [];
}

function readInitialTransport() {
  const raw = getInitialData().transport;
  if (!raw) return {
    recording: false, recordingLength: 0, recordingSampleRate: 0,
    playingSnippetId: -1, playingPosition: 0,
    inputLevel: 0, inputPeak: 0,
    libraryFolder: "", lastSaveError: "",
    metronomeEnabled: true, bpm: 120, countInBeats: 4,
    preRollActive: false, transportPosition: 0,
  };
  return {
    ...raw,
    inputLevel: Number(raw.inputLevel ?? 0),
    inputPeak: Number(raw.inputPeak ?? 0),
    recording: Boolean(raw.recording),
    recordingLength: Number(raw.recordingLength ?? 0),
    recordingSampleRate: Number(raw.recordingSampleRate ?? 0),
    playingSnippetId: Number(raw.playingSnippetId ?? -1),
    playingPosition: Number(raw.playingPosition ?? 0),
    metronomeEnabled: raw.metronomeEnabled !== false,
    bpm: Number(raw.bpm ?? 120),
    countInBeats: Number(raw.countInBeats ?? 4),
    preRollActive: Boolean(raw.preRollActive),
    transportPosition: Number(raw.transportPosition ?? 0),
  };
}

export default function App() {
  const initial = useMemo(readInitialParameters, []);
  const [gain, setGain] = useState(initial.gain);
  const [snippets, setSnippets] = useState(readInitialSnippets);
  const [transport, setTransport] = useState(readInitialTransport);
  const [notification, setNotification] = useState(null);
  const [vst3, setVst3] = useState({
    chain: { midiChain: { slots: [] }, audioChain: { slots: [] }, openEditors: [] },
    available: [],
    defaultFolder: "",
  });
  const [scanState, setScanState] = useState({ active: false, current: 0, total: 0, currentFile: "", folder: "" });

  useEffect(() => {
    const unsubParam = subscribe(BACKEND_EVENTS.parameters, (payload) => {
      if (typeof payload !== "object" || payload == null) return;
      if (payload.gain !== undefined) setGain(Number(payload.gain));
    });
    return unsubParam;
  }, []);

  useEffect(() => {
    const unsubSnippets = subscribe(BACKEND_EVENTS.snippets, (payload) => {
      if (Array.isArray(payload)) {
        setSnippets(payload);
        return;
      }
      if (typeof payload === "object" && payload !== null) {
        if (Array.isArray(payload.snippets)) setSnippets(payload.snippets);
        setTransport((prev) => ({
          ...prev,
          libraryFolder: payload.libraryFolder ?? prev.libraryFolder,
          lastSaveError: payload.lastSaveError ?? "",
        }));
      }
    });
    return unsubSnippets;
  }, []);

  // Ask the backend for a fresh snippet snapshot once the page is
  // ready. The processor's restoreUserState() loads snippets from
  // disk in its constructor — before the editor is constructed —
  // so the libraryChanged notification has no listener attached
  // and is lost. The withInitialisationData blob can also race the
  // page mount, so we don't rely on it. This explicit request is
  // the same pattern the chain UI uses for frontendGetVst3Chain.
  useEffect(() => {
    emit(FRONTEND_EVENTS.getSnippets);
  }, []);

  useEffect(() => {
    const unsubTransport = subscribe(BACKEND_EVENTS.transport, (payload) => {
      if (typeof payload !== "object" || payload == null) return;
      setTransport((prev) => ({
        ...prev,
        recording: Boolean(payload.recording),
        recordingLength: Number(payload.recordingLength ?? 0),
        recordingSampleRate: Number(payload.recordingSampleRate ?? prev.recordingSampleRate),
        playingSnippetId: Number(payload.playingSnippetId ?? -1),
        playingPosition: Number(payload.playingPosition ?? 0),
        inputLevel: Number(payload.inputLevel ?? 0),
        inputPeak: Number(payload.inputPeak ?? 0),
        libraryFolder: payload.libraryFolder ?? prev.libraryFolder,
        lastSaveError: payload.lastSaveError ?? prev.lastSaveError,
        metronomeEnabled: payload.metronomeEnabled !== undefined ? Boolean(payload.metronomeEnabled) : prev.metronomeEnabled,
        bpm:              payload.bpm !== undefined              ? Number(payload.bpm)              : prev.bpm,
        countInBeats:     payload.countInBeats !== undefined     ? Number(payload.countInBeats)     : prev.countInBeats,
        preRollActive:    Boolean(payload.preRollActive),
        transportPosition: Number(payload.transportPosition ?? 0),
      }));
    });
    return unsubTransport;
  }, []);

  useEffect(() => {
    const unsubNotify = subscribe(BACKEND_EVENTS.notify, (payload) => {
      if (typeof payload !== "object" || payload == null) return;
      setNotification({ message: String(payload.message ?? ""), level: String(payload.level ?? "info") });
    });
    return unsubNotify;
  }, []);

  useEffect(() => {
    const unsubChain = subscribe(BACKEND_EVENTS.vst3Chain, (payload) => {
      if (typeof payload !== "object" || payload == null) return;
      // The backend ships a single bundle per snapshot. Both chains
      // are always present (possibly empty). Old single-chain
      // snapshots aren't expected from the C++ side any more, but
      // the { chain: { slots } } shape is kept as a defensive
      // fallback so the UI doesn't blank out if a stale payload
      // sneaks in.
      const midiChain  = payload.midiChain  ?? { slots: [] };
      const audioChain = payload.audioChain ?? (payload.chain ?? { slots: [] });
      const openEditors = Array.isArray(payload.openEditors) ? payload.openEditors : [];
      const available = Array.isArray(payload.plugins) ? payload.plugins : [];
      const defaultFolder = typeof payload.folder === "string" ? payload.folder : "";
      setVst3({ chain: { midiChain, audioChain, openEditors }, available, defaultFolder });
    });
    return unsubChain;
  }, []);

  useEffect(() => {
    const unsubScan = subscribe(BACKEND_EVENTS.vst3ScanProgress, (payload) => {
      if (typeof payload !== "object" || payload == null) return;
      setScanState({
        active: Boolean(payload.active),
        current: Number(payload.current ?? 0),
        total: Number(payload.total ?? 0),
        currentFile: typeof payload.currentFile === "string" ? payload.currentFile : "",
        folder: typeof payload.folder === "string" ? payload.folder : "",
      });
    });
    return unsubScan;
  }, []);

  const handleGainChange = (next) => {
    setGain(next);
    emit(FRONTEND_EVENTS.setParameter, { id: PARAM_IDS.gain, value: next });
  };

  const handleMetronomeChange = (enabled) => {
    setTransport((prev) => ({ ...prev, metronomeEnabled: enabled }));
    emit(FRONTEND_EVENTS.setMetronome, { enabled });
  };

  const handleBpmChange = (next) => {
    setTransport((prev) => ({ ...prev, bpm: next }));
    emit(FRONTEND_EVENTS.setBpm, { bpm: next });
  };

  const handleCountInBeatsChange = (next) => {
    setTransport((prev) => ({ ...prev, countInBeats: next }));
    emit(FRONTEND_EVENTS.setCountInBeats, { beats: next });
  };

  const playingSnippet = transport.playingSnippetId >= 0 ? snippets.find((s) => s.id === transport.playingSnippetId) : null;
  const playPositionSeconds = playingSnippet && playingSnippet.sampleRate > 0
    ? transport.playingPosition / playingSnippet.sampleRate
    : 0;

  return (
    <main className="app">
      <header className="app-header">
        <div className="app-brand">
          <img src={iconUrl} alt="BluePrinter logo" className="app-logo" />
          <div className="app-brand-text">
            <h1>BluePrinter</h1>
            <p>Record a take, name it, note what to work on.</p>
          </div>
        </div>
      </header>

      <Transport
        transport={transport}
        gain={gain}
        onGainChange={handleGainChange}
        metronomeEnabled={transport.metronomeEnabled}
        bpm={transport.bpm}
        countInBeats={transport.countInBeats}
        onMetronomeChange={handleMetronomeChange}
        onBpmChange={handleBpmChange}
        onCountInBeatsChange={handleCountInBeatsChange}
      />

      <PluginChain
        chainState={vst3.chain}
        availablePlugins={vst3.available}
        defaultFolder={vst3.defaultFolder}
        scanState={scanState}
      />

      <div className="library-section">
        <LibraryFolderRow
          folder={transport.libraryFolder}
          error={transport.lastSaveError}
        />

        <SnippetList
          snippets={snippets}
          playingSnippetId={transport.playingSnippetId}
          playPositionSeconds={playPositionSeconds}
        />
      </div>

      <Notification
        notification={notification}
        onDismiss={() => setNotification(null)}
      />

      <footer className="app-footer">
        Recordings stay in memory until you save them — pick a library folder for one-click auto-save.
      </footer>
    </main>
  );
}
