import { useEffect, useMemo, useRef, useState } from "react";
import { Transport } from "./components/Transport";
import { HeaderControls } from "./components/HeaderControls";
import { SyncControls } from "./components/SyncControls";
import { TakeReview } from "./components/TakeReview";
import { LibraryFolderRow } from "./components/LibraryFolderRow";
import { SnippetList } from "./components/SnippetList";
import { Notification } from "./components/Notification";
import { PluginChain } from "./components/PluginChain";
import { Looper } from "./components/Looper";
import { SplashScreen } from "./components/SplashScreen";
import { ErrorBoundary } from "./components/ErrorBoundary";
import { BACKEND_EVENTS, FRONTEND_EVENTS, emit, getInitialData, subscribe } from "./bridge";
import iconUrl from "./icon.svg";

const PARAM_IDS = {
  gain: "Gain",
  playbackVolume: "PlaybackVolume",
};

// Splash timings: hold the branded splash for at least this long so a fast
// (chain-less) startup still feels intentional, then fade out over this.
const SPLASH_MIN_MS = 900;
const SPLASH_FADE_MS = 400;

// Which recording approach the tab bar shows: the one-shot take recorder
// or the looper. Persisted so the plugin reopens where you left off.
const RECORDING_MODE_KEY = "bp:recordingMode";

function readInitialRecordingMode() {
  return localStorage.getItem(RECORDING_MODE_KEY) === "loop" ? "loop" : "take";
}

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

function readInitialTagNames() {
  const raw = getInitialData().tagNames;
  if (raw && typeof raw === "object") return raw;
  return {};
}

function readInitialTransport() {
  const raw = getInitialData().transport;
  if (!raw) return {
    recording: false, recordingLength: 0, recordingSampleRate: 0,
    playingSnippetId: -1, playingPosition: 0,
    inputLevel: 0, inputPeak: 0,
    libraryFolder: "", lastSaveError: "",
    metronomeEnabled: true, bpm: 120, countInBeats: 4, dryLevel: 1, clickDuringCapture: true,
    clickPitch: 1000, clickAccentPitch: 1500, clickDecay: 90, clickVolume: 0.35, clickAccentVolume: 0.5, clickNoise: 0.1,
    midiClockEnabled: false, midiClockOnRecord: false, midiOutputDevice: "", midiOutputDeviceList: [],
     preRollActive: false, transportPosition: 0,
     takePending: false, takeLength: 0, takePlaying: false, takePosition: 0, takePeaks: [],
     looperRecording: false, looperPreRoll: false, looperPlaying: false, looperLooping: true, looperOverdub: false, looperCountInBeats: 4, looperCropStartBeats: 0, looperCropEndBeats: 0, audioLoopStart: 0, audioLoopPosition: 0, audioLoopLength: 0, audioLoopPeaks: [], chainLevels: [], maxRecordSamples: 0,
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
    clickDuringCapture: raw.clickDuringCapture !== false,
    clickPitch: Number(raw.clickPitch ?? 1000),
    clickAccentPitch: Number(raw.clickAccentPitch ?? 1500),
    clickDecay: Number(raw.clickDecay ?? 90),
    clickVolume: Number(raw.clickVolume ?? 0.35),
    clickAccentVolume: Number(raw.clickAccentVolume ?? 0.5),
    clickNoise: Number(raw.clickNoise ?? 0.1),
    midiClockEnabled: Boolean(raw.midiClockEnabled),
    midiClockOnRecord: Boolean(raw.midiClockOnRecord),
    midiOutputDevice: typeof raw.midiOutputDevice === "string" ? raw.midiOutputDevice : "",
    midiOutputDeviceList: Array.isArray(raw.midiOutputDeviceList) ? raw.midiOutputDeviceList : [],
     preRollActive: Boolean(raw.preRollActive),
     transportPosition: Number(raw.transportPosition ?? 0),
     takePending: Boolean(raw.takePending),
     takeLength: Number(raw.takeLength ?? 0),
     takePlaying: Boolean(raw.takePlaying),
     takePosition: Number(raw.takePosition ?? 0),
     takePeaks: Array.isArray(raw.takePeaks) ? raw.takePeaks : [],
     looperRecording: Boolean(raw.looperRecording), looperPreRoll: Boolean(raw.looperPreRoll), looperPlaying: Boolean(raw.looperPlaying), looperLooping: raw.looperLooping !== false, looperOverdub: Boolean(raw.looperOverdub), looperCountInBeats: Number(raw.looperCountInBeats ?? 4), looperCropStartBeats: Number(raw.looperCropStartBeats ?? 0), looperCropEndBeats: Number(raw.looperCropEndBeats ?? 0), audioLoopStart: Number(raw.audioLoopStart ?? 0), audioLoopPosition: Number(raw.audioLoopPosition ?? 0), audioLoopLength: Number(raw.audioLoopLength ?? 0), audioLoopPeaks: Array.isArray(raw.audioLoopPeaks) ? raw.audioLoopPeaks : [], chainLevels: Array.isArray(raw.chainLevels) ? raw.chainLevels : [], maxRecordSamples: Number(raw.maxRecordSamples ?? 0),
  };
}

export default function App() {
  const initial = useMemo(readInitialParameters, []);
  const [gain, setGain] = useState(initial.gain);
  const [playbackVolume, setPlaybackVolume] = useState(initial.playbackVolume);
  const [snippets, setSnippets] = useState(readInitialSnippets);
  const [tagNames, setTagNames] = useState(readInitialTagNames);
  const [transport, setTransport] = useState(readInitialTransport);
  const [notification, setNotification] = useState(null);
  const [vst3, setVst3] = useState({
    chains: [],
    openEditors: [],
    available: [],
    defaultFolder: "",
    inputChannels: 2,
    restoring: false,
    restoreError: "",
  });
  const [scanState, setScanState] = useState({ active: false, current: 0, total: 0, currentFile: "", folder: "" });
  const [recordingMode, setRecordingMode] = useState(readInitialRecordingMode);

  const handleRecordingModeChange = (mode) => {
    setRecordingMode(mode);
    localStorage.setItem(RECORDING_MODE_KEY, mode);
  };

  // WAI-ARIA tabs pattern: Left/Right arrows cycle the tab and move
  // focus with it; only the active tab is in the tab order.
  const handleRecordingTabsKeyDown = (e) => {
    if (e.key !== "ArrowLeft" && e.key !== "ArrowRight") return;
    e.preventDefault();
    const next = e.key === "ArrowRight"
      ? (recordingMode === "take" ? "loop" : "take")
      : (recordingMode === "loop" ? "take" : "loop");
    handleRecordingModeChange(next);
    document.getElementById(`recording-tab-${next}`)?.focus();
  };

  // Splash lifecycle. `showing` -> `leaving` (fade) -> `hidden`. The splash
  // stays up until the first chain snapshot arrives (covering the WebView2
  // boot + first round trip) and then until any deferred plugin restore
  // finishes — the backend loads saved VST3s one at a time on startup.
  const [splashPhase, setSplashPhase] = useState("showing");
  const [chainSnapshotReceived, setChainSnapshotReceived] = useState(false);
  const mountedAt = useRef(Date.now());

  useEffect(() => {
    if (!chainSnapshotReceived || splashPhase !== "showing") return;
    if (vst3.restoring) return;
    const elapsed = Date.now() - mountedAt.current;
    const delay = Math.max(0, SPLASH_MIN_MS - elapsed);
    const t = setTimeout(() => setSplashPhase("leaving"), delay);
    return () => clearTimeout(t);
  }, [chainSnapshotReceived, splashPhase, vst3.restoring]);

  useEffect(() => {
    if (splashPhase !== "leaving") return;
    const t = setTimeout(() => setSplashPhase("hidden"), SPLASH_FADE_MS);
    return () => clearTimeout(t);
  }, [splashPhase]);

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
        if (payload.tagNames && typeof payload.tagNames === "object") setTagNames(payload.tagNames);
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

  // Same pattern for the chain snapshot — the splash needs it to know
  // whether a deferred plugin restore is in flight. The emit from
  // PluginChain coalesces with this one into a single snapshot.
  useEffect(() => {
    emit(FRONTEND_EVENTS.getVst3Chain);
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
        clickDuringCapture: payload.clickDuringCapture !== undefined ? Boolean(payload.clickDuringCapture) : prev.clickDuringCapture,
        clickPitch:        payload.clickPitch        !== undefined ? Number(payload.clickPitch)        : prev.clickPitch,
        clickAccentPitch:  payload.clickAccentPitch  !== undefined ? Number(payload.clickAccentPitch)  : prev.clickAccentPitch,
        clickDecay:        payload.clickDecay        !== undefined ? Number(payload.clickDecay)        : prev.clickDecay,
        clickVolume:       payload.clickVolume       !== undefined ? Number(payload.clickVolume)       : prev.clickVolume,
        clickAccentVolume: payload.clickAccentVolume !== undefined ? Number(payload.clickAccentVolume) : prev.clickAccentVolume,
        clickNoise:        payload.clickNoise        !== undefined ? Number(payload.clickNoise)        : prev.clickNoise,
        midiClockEnabled: payload.midiClockEnabled !== undefined ? Boolean(payload.midiClockEnabled) : prev.midiClockEnabled,
        midiClockOnRecord: payload.midiClockOnRecord !== undefined ? Boolean(payload.midiClockOnRecord) : prev.midiClockOnRecord,
        midiOutputDevice: typeof payload.midiOutputDevice === "string" ? payload.midiOutputDevice : prev.midiOutputDevice,
        midiOutputDeviceList: Array.isArray(payload.midiOutputDeviceList) ? payload.midiOutputDeviceList : prev.midiOutputDeviceList,
        preRollActive:    Boolean(payload.preRollActive),
         transportPosition: Number(payload.transportPosition ?? 0),
         takePending: payload.takePending !== undefined ? Boolean(payload.takePending) : prev.takePending,
         takeLength: payload.takeLength !== undefined ? Number(payload.takeLength) : prev.takeLength,
         takePlaying: payload.takePlaying !== undefined ? Boolean(payload.takePlaying) : prev.takePlaying,
         takePosition: Number(payload.takePosition ?? prev.takePosition ?? 0),
         takePeaks: Array.isArray(payload.takePeaks) ? payload.takePeaks : (prev.takePeaks ?? []),
         looperRecording: payload.looperRecording !== undefined ? Boolean(payload.looperRecording) : prev.looperRecording,
         looperPreRoll: payload.looperPreRoll !== undefined ? Boolean(payload.looperPreRoll) : prev.looperPreRoll,
         looperPlaying: payload.looperPlaying !== undefined ? Boolean(payload.looperPlaying) : prev.looperPlaying,
         looperLooping: payload.looperLooping !== undefined ? Boolean(payload.looperLooping) : prev.looperLooping,
         looperOverdub: payload.looperOverdub !== undefined ? Boolean(payload.looperOverdub) : prev.looperOverdub,
         maxRecordSamples: payload.maxRecordSamples !== undefined ? Number(payload.maxRecordSamples) : prev.maxRecordSamples,
         looperCountInBeats: payload.looperCountInBeats !== undefined ? Number(payload.looperCountInBeats) : prev.looperCountInBeats,
         looperCropStartBeats: payload.looperCropStartBeats !== undefined ? Number(payload.looperCropStartBeats) : prev.looperCropStartBeats,
         looperCropEndBeats: payload.looperCropEndBeats !== undefined ? Number(payload.looperCropEndBeats) : prev.looperCropEndBeats,
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
          pending: Number(base.pending ?? 0),
          slots: Array.isArray(base.slots) ? base.slots : [],
        };
      };

      const openEditors = Array.isArray(payload.openEditors) ? payload.openEditors : [];
      const available = Array.isArray(payload.plugins) ? payload.plugins : [];
      const defaultFolder = typeof payload.folder === "string" ? payload.folder : "";
      const inputChannels = Number.isFinite(payload.inputChannels) ? Number(payload.inputChannels) : 2;

      setChainSnapshotReceived(true);
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
        return {
          chains,
          openEditors,
          available,
          defaultFolder,
          inputChannels,
          restoring: payload.restoring !== undefined ? Boolean(payload.restoring) : prev.restoring,
          restoreError: typeof payload.restoreError === "string" ? payload.restoreError : prev.restoreError,
        };
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

  const handleMidiClockOnRecordChange = (enabled) => {
    setTransport((prev) => ({ ...prev, midiClockOnRecord: enabled }));
    emit(FRONTEND_EVENTS.setMidiClockOnRecord, { enabled });
  };

  const handleMidiDeviceChange = (device) => {
    setTransport((prev) => ({ ...prev, midiOutputDevice: device }));
    emit(FRONTEND_EVENTS.setMidiDevice, { device });
  };

  const handleClickDuringCaptureChange = (enabled) => {
    setTransport((prev) => ({ ...prev, clickDuringCapture: enabled }));
    emit(FRONTEND_EVENTS.setClickDuringCapture, { enabled });
  };

  const handleLooperOverdubChange = (enabled) => {
    setTransport((prev) => ({ ...prev, looperOverdub: enabled }));
    emit(FRONTEND_EVENTS.setLooperOverdub, { enabled });
  };

  const handleRenameTag = (color, name) => {
    // Optimistic local update; the backend persists and echoes the map.
    setTagNames((prev) => {
      const next = { ...prev };
      const trimmed = String(name ?? "").trim();
      if (trimmed) next[color] = trimmed;
      else delete next[color];
      return next;
    });
    emit(FRONTEND_EVENTS.renameTag, { color, name: String(name ?? "") });
  };

  const playingSnippet = transport.playingSnippetId >= 0 ? snippets.find((s) => s.id === transport.playingSnippetId) : null;
  const playPositionSeconds = playingSnippet && playingSnippet.sampleRate > 0
    ? transport.playingPosition / playingSnippet.sampleRate
    : 0;

  // Restore progress for the splash: total = loaded slots + pending loads.
  const { pendingTotal, totalPlugins } = useMemo(() => {
    let pending = 0;
    let total = 0;
    for (const chain of vst3.chains) {
      const p = Number(chain.pending ?? 0);
      pending += p;
      total += (Array.isArray(chain.slots) ? chain.slots.length : 0) + p;
    }
    return { pendingTotal: pending, totalPlugins: total };
  }, [vst3.chains]);
  const restoreProgress = totalPlugins > 0 ? ((totalPlugins - pendingTotal) / totalPlugins) * 100 : 0;
  const restoringChains = vst3.chains
    .filter((chain) => Number(chain.pending ?? 0) > 0)
    .map((chain) => ({ id: chain.id, name: chain.name, pending: Number(chain.pending) }));

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
        <HeaderControls
          gain={gain}
          onGainChange={handleGainChange}
          playbackVolume={playbackVolume}
          onPlaybackVolumeChange={handlePlaybackVolumeChange}
          dryLevel={transport.dryLevel}
          onDryLevelChange={handleDryLevelChange}
          bpm={transport.bpm}
          onBpmChange={handleBpmChange}
        />
      </header>

      <div className="record-tools">
        <div
          className="recording-tabs"
          role="tablist"
          aria-label="Recording approach"
          onKeyDown={handleRecordingTabsKeyDown}
        >
          <button
            type="button"
            role="tab"
            id="recording-tab-take"
            aria-selected={recordingMode === "take"}
            aria-controls="recording-panel-take"
            tabIndex={recordingMode === "take" ? 0 : -1}
            className={`recording-tab ${recordingMode === "take" ? "is-active" : ""}`}
            onClick={() => handleRecordingModeChange("take")}
          >
            Take
            {transport.takePending ? (
              <span className="recording-tab-badge" title="Unsaved take — review it" aria-label="Unsaved take pending" />
            ) : null}
          </button>
          <button
            type="button"
            role="tab"
            id="recording-tab-loop"
            aria-selected={recordingMode === "loop"}
            aria-controls="recording-panel-loop"
            tabIndex={recordingMode === "loop" ? 0 : -1}
            className={`recording-tab ${recordingMode === "loop" ? "is-active" : ""}`}
            onClick={() => handleRecordingModeChange("loop")}
          >
            Loop
          </button>
        </div>

        <SyncControls
          metronomeEnabled={transport.metronomeEnabled !== false}
          onMetronomeChange={handleMetronomeChange}
          clickDuringCapture={transport.clickDuringCapture !== false}
          onClickDuringCaptureChange={handleClickDuringCaptureChange}
          clickParams={transport}
          midiClockEnabled={Boolean(transport.midiClockEnabled)}
          onMidiClockChange={handleMidiClockChange}
          midiClockOnRecord={Boolean(transport.midiClockOnRecord)}
          onMidiClockOnRecordChange={handleMidiClockOnRecordChange}
          midiOutputDevice={transport.midiOutputDevice}
          midiOutputDeviceList={transport.midiOutputDeviceList}
          onMidiDeviceChange={handleMidiDeviceChange}
        />
      </div>

      {recordingMode === "take" ? (
        <div id="recording-panel-take" role="tabpanel" aria-labelledby="recording-tab-take">
          <Transport
            transport={transport}
            bpm={transport.bpm}
            countInBeats={transport.countInBeats}
            onCountInBeatsChange={handleCountInBeatsChange}
          />

          <TakeReview transport={transport} />
        </div>
      ) : (
        <div id="recording-panel-loop" role="tabpanel" aria-labelledby="recording-tab-loop">
          <Looper
            transport={transport}
            onOverdubChange={handleLooperOverdubChange}
          />
        </div>
      )}

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
            tagNames={tagNames}
            onRenameTag={handleRenameTag}
            playingSnippetId={transport.playingSnippetId}
            playPositionSeconds={playPositionSeconds}
          />
        </ErrorBoundary>
      </div>

      <Notification
        notification={notification}
        onDismiss={() => setNotification(null)}
      />

      <SplashScreen
        visible={splashPhase !== "hidden"}
        leaving={splashPhase === "leaving"}
        restoring={Boolean(vst3.restoring)}
        snapshotReceived={chainSnapshotReceived}
        progress={restoreProgress}
        remaining={pendingTotal}
        total={totalPlugins}
        chains={restoringChains}
        error={vst3.restoreError}
      />

      <footer className="app-footer">
        Takes stay in memory until you save or discard them — pick a library folder so saves have a destination.
      </footer>
    </main>
  );
}
