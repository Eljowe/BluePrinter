import { useEffect, useMemo, useState } from "react";
import { Transport } from "./components/Transport";
import { LibraryFolderRow } from "./components/LibraryFolderRow";
import { SnippetList } from "./components/SnippetList";
import { Notification } from "./components/Notification";
import { BACKEND_EVENTS, FRONTEND_EVENTS, emit, getInitialData, subscribe } from "./bridge";

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
        <div>
          <h1>BluePrinter</h1>
          <p>Record a guitar take, name it, write down what to work on.</p>
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

      <LibraryFolderRow
        folder={transport.libraryFolder}
        error={transport.lastSaveError}
      />

      <SnippetList
        snippets={snippets}
        playingSnippetId={transport.playingSnippetId}
        playPositionSeconds={playPositionSeconds}
      />

      <Notification
        notification={notification}
        onDismiss={() => setNotification(null)}
      />

      <footer className="app-footer">
        Recordings stay in memory until you save them. Pick a library folder for one-click auto-save.
      </footer>
    </main>
  );
}
