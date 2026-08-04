import { useEffect, useMemo, useState } from "react";
import { Transport } from "./components/Transport";
import { LibraryFolderRow } from "./components/LibraryFolderRow";
import { SnippetList } from "./components/SnippetList";
import { Notification } from "./components/Notification";
import { PluginChain } from "./components/PluginChain";
import { Looper } from "./components/Looper";
import { MidiClock } from "./components/MidiClock";
import { ErrorBoundary } from "./components/ErrorBoundary";
import { BACKEND_EVENTS, FRONTEND_EVENTS, emit, getInitialData, subscribe } from "./bridge";
import iconUrl from "./icon.svg";

const PARAM_IDS = {
  gain: "Gain",
  playbackVolume: "PlaybackVolume",
};

function readInitialParameters() {
  const first = getInitialData().parameters?.[0];
  if (!first) return { gain: 0.7, playbackVolume: 0.8 };
  return {
    gain: Number(first.gain ?? 0.7),
    playbackVolume: Number(first.playbackVolume ?? 0.8),
  };
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
    metronomeEnabled: true, bpm: 120, countInBeats: 4, dryLevel: 1,
    clickPitch: 1000, clickAccentPitch: 1500, clickDecay: 90, clickVolume: 0.35, clickAccentVolume: 0.5, clickNoise: 0.1,
    midiClockEnabled: false, midiOutputDevice: "", midiOutputDeviceList: [],
     preRollActive: false, transportPosition: 0,
     looperRecording: false, looperPreRoll: false, looperPlaying: false, looperLooping: true, looperClickEnabled: true, looperCountInBeats: 4, looperCropStartBars: 0, looperCropEndBars: 0, audioLoopStart: 0, audioLoopPosition: 0, audioLoopLength: 0, audioLoopPeaks: [], chainLevels: [],
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
    dryLevel: Number(raw.dryLevel ?? 1),
    clickPitch: Number(raw.clickPitch ?? 1000),
    clickAccentPitch: Number(raw.clickAccentPitch ?? 1500),
    clickDecay: Number(raw.clickDecay ?? 90),
    clickVolume: Number(raw.clickVolume ?? 0.35),
    clickAccentVolume: Number(raw.clickAccentVolume ?? 0.5),
    clickNoise: Number(raw.clickNoise ?? 0.1),
    midiClockEnabled: Boolean(raw.midiClockEnabled),
    midiOutputDevice: typeof raw.midiOutputDevice === "string" ? raw.midiOutputDevice : "",
    midiOutputDeviceList: Array.isArray(raw.midiOutputDeviceList) ? raw.midiOutputDeviceList : [],
     preRollActive: Boolean(raw.preRollActive),
     transportPosition: Number(raw.transportPosition ?? 0),
     looperRecording: Boolean(raw.looperRecording), looperPreRoll: Boolean(raw.looperPreRoll), looperPlaying: Boolean(raw.looperPlaying), looperLooping: raw.looperLooping !== false, looperClickEnabled: raw.looperClickEnabled !== false, looperCountInBeats: Number(raw.looperCountInBeats ?? 4), looperCropStartBars: Number(raw.looperCropStartBars ?? 0), looperCropEndBars: Number(raw.looperCropEndBars ?? 0), audioLoopStart: Number(raw.audioLoopStart ?? 0), audioLoopPosition: Number(raw.audioLoopPosition ?? 0), audioLoopLength: Number(raw.audioLoopLength ?? 0), audioLoopPeaks: Array.isArray(raw.audioLoopPeaks) ? raw.audioLoopPeaks : [], chainLevels: Array.isArray(raw.chainLevels) ? raw.chainLevels : [],
  };
}

export default function App() {
  const initial = useMemo(readInitialParameters, []);
  const [gain, setGain] = useState(initial.gain);
  const [playbackVolume, setPlaybackVolume] = useState(initial.playbackVolume);
  const [snippets, setSnippets] = useState(readInitialSnippets);
  const [transport, setTransport] = useState(readInitialTransport);
  const [notification, setNotification] = useState(null);
  const [vst3, setVst3] = useState({
    chains: [],
    openEditors: [],
    available: [],
    defaultFolder: "",
    inputChannels: 2,
  });
  const [scanState, setScanState] = useState({ active: false, current: 0, total: 0, currentFile: "", folder: "" });

  useEffect(() => {
    const unsubParam = subscribe(BACKEND_EVENTS.parameters, (payload) => {
      if (typeof payload !== "object" || payload == null) return;
      if (payload.gain !== undefined) setGain(Number(payload.gain));
      if (payload.playbackVolume !== undefined) setPlaybackVolume(Number(payload.playbackVolume));
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
        dryLevel:         payload.dryLevel !== undefined         ? Number(payload.dryLevel)         : prev.dryLevel,
        clickPitch:        payload.clickPitch        !== undefined ? Number(payload.clickPitch)        : prev.clickPitch,
        clickAccentPitch:  payload.clickAccentPitch  !== undefined ? Number(payload.clickAccentPitch)  : prev.clickAccentPitch,
        clickDecay:        payload.clickDecay        !== undefined ? Number(payload.clickDecay)        : prev.clickDecay,
        clickVolume:       payload.clickVolume       !== undefined ? Number(payload.clickVolume)       : prev.clickVolume,
        clickAccentVolume: payload.clickAccentVolume !== undefined ? Number(payload.clickAccentVolume) : prev.clickAccentVolume,
        clickNoise:        payload.clickNoise        !== undefined ? Number(payload.clickNoise)        : prev.clickNoise,
        midiClockEnabled: payload.midiClockEnabled !== undefined ? Boolean(payload.midiClockEnabled) : prev.midiClockEnabled,
        midiOutputDevice: typeof payload.midiOutputDevice === "string" ? payload.midiOutputDevice : prev.midiOutputDevice,
        midiOutputDeviceList: Array.isArray(payload.midiOutputDeviceList) ? payload.midiOutputDeviceList : prev.midiOutputDeviceList,
        preRollActive:    Boolean(payload.preRollActive),
         transportPosition: Number(payload.transportPosition ?? 0),
         looperRecording: payload.looperRecording !== undefined ? Boolean(payload.looperRecording) : prev.looperRecording,
         looperPreRoll: payload.looperPreRoll !== undefined ? Boolean(payload.looperPreRoll) : prev.looperPreRoll,
         looperPlaying: payload.looperPlaying !== undefined ? Boolean(payload.looperPlaying) : prev.looperPlaying,
         looperLooping: payload.looperLooping !== undefined ? Boolean(payload.looperLooping) : prev.looperLooping,
         looperClickEnabled: payload.looperClickEnabled !== undefined ? Boolean(payload.looperClickEnabled) : prev.looperClickEnabled,
         looperCountInBeats: payload.looperCountInBeats !== undefined ? Number(payload.looperCountInBeats) : prev.looperCountInBeats,
         looperCropStartBars: payload.looperCropStartBars !== undefined ? Number(payload.looperCropStartBars) : prev.looperCropStartBars,
         looperCropEndBars: payload.looperCropEndBars !== undefined ? Number(payload.looperCropEndBars) : prev.looperCropEndBars,
         audioLoopStart: Number(payload.audioLoopStart ?? prev.audioLoopStart ?? 0),
         audioLoopPosition: Number(payload.audioLoopPosition ?? prev.audioLoopPosition ?? 0),
         audioLoopLength: Number(payload.audioLoopLength ?? prev.audioLoopLength ?? 0),
         audioLoopPeaks: Array.isArray(payload.audioLoopPeaks) ? payload.audioLoopPeaks : (prev.audioLoopPeaks ?? []),
         chainLevels: Array.isArray(payload.chainLevels) ? payload.chainLevels : (prev.chainLevels ?? []),
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

      const normalizeChain = (raw, fallback) => {
        const base = raw && typeof raw === "object" ? raw : {};
        const inputs = Array.isArray(base.inputs) ? base.inputs : (fallback?.inputs ?? [0, 1]);
        const midiChannels = Array.isArray(base.midiChannels)
          ? base.midiChannels
          : (fallback?.midiChannels ?? Array.from({ length: 16 }, (_, i) => i + 1));
        return {
          id: String(base.id ?? fallback?.id ?? ""),
          name: String(base.name ?? fallback?.name ?? ""),
          inputs,
          wantsMidi: base.wantsMidi !== undefined ? Boolean(base.wantsMidi) : (fallback?.wantsMidi ?? true),
          recordOnCapture: base.recordOnCapture !== undefined ? Boolean(base.recordOnCapture) : true,
          volume: Number(base.volume ?? 0),
          muted: Boolean(base.muted),
          midiChannels,
          slots: Array.isArray(base.slots) ? base.slots : [],
        };
      };

      const openEditors = Array.isArray(payload.openEditors) ? payload.openEditors : [];
      const available = Array.isArray(payload.plugins) ? payload.plugins : [];
      const defaultFolder = typeof payload.folder === "string" ? payload.folder : "";
      const inputChannels = Number.isFinite(payload.inputChannels) ? Number(payload.inputChannels) : 2;

      setVst3((prev) => {
        let chains = prev.chains;
        if (Array.isArray(payload.chains)) {
          chains = payload.chains.map((c) => normalizeChain(c, null));
        } else if (!Array.isArray(prev.chains) || prev.chains.length === 0) {
          // Legacy midiChain/audioChain-keyed payloads (old builds) are
          // migrated once; partial snapshots (e.g. scan progress) that
          // omit the chains array keep whatever chains we already have.
          const midi = payload.midiChain ?? null;
          const audio = payload.audioChain ?? payload.chain ?? null;
          chains = [
            normalizeChain(midi, { id: "chain0", name: "MIDI Chain", wantsMidi: true }),
            normalizeChain(audio, { id: "chain1", name: "Audio FX Chain", wantsMidi: false }),
          ];
        }
        return { chains, openEditors, available, defaultFolder, inputChannels };
      });
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

  const handlePlaybackVolumeChange = (next) => {
    setPlaybackVolume(next);
    emit(FRONTEND_EVENTS.setParameter, { id: PARAM_IDS.playbackVolume, value: next });
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

  const handleDryLevelChange = (next) => {
    setTransport((prev) => ({ ...prev, dryLevel: next }));
    emit(FRONTEND_EVENTS.setDryLevel, { level: next });
  };

  const handleMidiClockChange = (enabled) => {
    setTransport((prev) => ({ ...prev, midiClockEnabled: enabled }));
    emit(FRONTEND_EVENTS.setMidiClock, { enabled });
  };

  const handleMidiDeviceChange = (device) => {
    setTransport((prev) => ({ ...prev, midiOutputDevice: device }));
    emit(FRONTEND_EVENTS.setMidiDevice, { device });
  };

  const handleTakeMidiClockChange = (enabled) => {
    setTransport((prev) => ({ ...prev, takeMidiClock: enabled }));
    emit(FRONTEND_EVENTS.setTakeMidiClock, { enabled });
  };

  const handleClickDuringTakeChange = (enabled) => {
    setTransport((prev) => ({ ...prev, clickDuringTake: enabled }));
    emit(FRONTEND_EVENTS.setClickDuringTake, { enabled });
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
        playbackVolume={playbackVolume}
        onPlaybackVolumeChange={handlePlaybackVolumeChange}
        metronomeEnabled={transport.metronomeEnabled}
        bpm={transport.bpm}
        countInBeats={transport.countInBeats}
        dryLevel={transport.dryLevel}
        midiClockEnabled={transport.midiClockEnabled}
        takeMidiClock={transport.takeMidiClock}
        clickDuringTake={transport.clickDuringTake !== false}
        onMetronomeChange={handleMetronomeChange}
        onBpmChange={handleBpmChange}
        onCountInBeatsChange={handleCountInBeatsChange}
        onDryLevelChange={handleDryLevelChange}
        onTakeMidiClockChange={handleTakeMidiClockChange}
        onClickDuringTakeChange={handleClickDuringTakeChange}
      />

      <MidiClock
        enabled={transport.midiClockEnabled}
        device={transport.midiOutputDevice}
        deviceList={transport.midiOutputDeviceList}
        bpm={transport.bpm}
        onStartStop={() => handleMidiClockChange(!transport.midiClockEnabled)}
        onDeviceChange={handleMidiDeviceChange}
      />

      <Looper transport={transport} />


      <PluginChain
        chainState={{ chains: vst3.chains, openEditors: vst3.openEditors }}
        inputChannels={vst3.inputChannels}
        chainLevels={transport.chainLevels}
        availablePlugins={vst3.available}
        defaultFolder={vst3.defaultFolder}
        scanState={scanState}
      />

      <div className="library-section">
        <ErrorBoundary>
          <LibraryFolderRow
            folder={transport.libraryFolder}
            error={transport.lastSaveError}
          />
        </ErrorBoundary>

        <ErrorBoundary>
          <SnippetList
            snippets={snippets}
            playingSnippetId={transport.playingSnippetId}
            playPositionSeconds={playPositionSeconds}
          />
        </ErrorBoundary>
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
