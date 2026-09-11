import { useEffect, useMemo, useRef, useState } from "react";
import { createPortal } from "react-dom";
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
  input: "Gain",
  output: "PlaybackVolume",
};

// Clip-reset target -> transport state key.
const CLIP_KEYS = {
  input: "inputClipped",
  record: "recordClipped",
  output: "outputClipped",
  loop: "loopClipped",
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
  const raw = getInitialData().parameters;
  const first = Array.isArray(raw) ? raw[0] : raw;
  if (!first) return { input: 0, output: 0 };
  return {
    input: Number(first.input ?? 0),
    output: Number(first.output ?? 0),
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
    recordLevel: 0, recordPeak: 0,
    outputLevel: 0, outputPeak: 0,
    loopPlayLevel: 0, loopPlayPeak: 0,
    inputClipped: false, recordClipped: false, outputClipped: false, loopClipped: false,
    libraryFolder: "", lastSaveError: "",
    metronomeEnabled: true, bpm: 120, countInBeats: 4, loopLevel: 0, overdubLevel: 0, dryLevel: 0, clickDuringCapture: true,
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
    recordLevel: Number(raw.recordLevel ?? 0),
    recordPeak: Number(raw.recordPeak ?? 0),
    outputLevel: Number(raw.outputLevel ?? 0),
    outputPeak: Number(raw.outputPeak ?? 0),
    loopPlayLevel: Number(raw.loopPlayLevel ?? 0),
    loopPlayPeak: Number(raw.loopPlayPeak ?? 0),
    inputClipped: Boolean(raw.inputClipped),
    recordClipped: Boolean(raw.recordClipped),
    outputClipped: Boolean(raw.outputClipped),
    loopClipped: Boolean(raw.loopClipped),
    recording: Boolean(raw.recording),
    recordingLength: Number(raw.recordingLength ?? 0),
    recordingSampleRate: Number(raw.recordingSampleRate ?? 0),
    playingSnippetId: Number(raw.playingSnippetId ?? -1),
    playingPosition: Number(raw.playingPosition ?? 0),
    metronomeEnabled: raw.metronomeEnabled !== false,
    bpm: Number(raw.bpm ?? 120),
    countInBeats: Number(raw.countInBeats ?? 4),
    loopLevel: Number(raw.loopLevel ?? 0),
    overdubLevel: Number(raw.overdubLevel ?? 0),
    dryLevel: Number(raw.dryLevel ?? 0),
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
     looperRecording: Boolean(raw.looperRecording), looperPreRoll: Boolean(raw.looperPreRoll), looperPlaying: Boolean(raw.looperPlaying), looperLooping: raw.looperLooping !== false, looperOverdub: Boolean(raw.looperOverdub), looperCountInBeats: Number(raw.looperCountInBeats ?? 4), looperLengthBars: Number(raw.looperLengthBars ?? 0), looperCropStartBeats: Number(raw.looperCropStartBeats ?? 0), looperCropEndBeats: Number(raw.looperCropEndBeats ?? 0), audioLoopStart: Number(raw.audioLoopStart ?? 0), audioLoopPosition: Number(raw.audioLoopPosition ?? 0), audioLoopLength: Number(raw.audioLoopLength ?? 0), audioLoopPeaks: Array.isArray(raw.audioLoopPeaks) ? raw.audioLoopPeaks : [], chainLevels: Array.isArray(raw.chainLevels) ? raw.chainLevels : [], maxRecordSamples: Number(raw.maxRecordSamples ?? 0),
  };
}

export default function App() {
  const initial = useMemo(readInitialParameters, []);
  const [input, setInput] = useState(initial.input);
  const [output, setOutput] = useState(initial.output);
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

  // Global transport shortcuts (0020). Space starts/stops capture for the
  // active recording tab, Enter saves a pending take, Esc stops playback.
  // Ignored while typing or when focus is on a control, so Space/Enter still
  // activate the focused button/select instead of double-firing.
  useEffect(() => {
    const isEditable = (el) => {
      if (!el) return false;
      const tag = el.tagName;
      return tag === "INPUT" || tag === "TEXTAREA" || tag === "SELECT" || el.isContentEditable;
    };
    const isControl = (el) => Boolean(el && el.closest
      && el.closest("button, a, [role='button'], [role='tab'], [role='switch']"));

    const onKeyDown = (e) => {
      if (e.defaultPrevented || e.metaKey || e.ctrlKey || e.altKey) return;
      if (isEditable(e.target) || isEditable(document.activeElement)) return;

      if (e.key === " ") {
        if (isControl(document.activeElement)) return;
        e.preventDefault();
        if (recordingMode === "take") {
          const capturing = transport.recording || transport.preRollActive;
          if (capturing) emit(FRONTEND_EVENTS.stopRecording);
          else emit(FRONTEND_EVENTS.startRecording);
        } else {
          const capturing = transport.looperRecording || transport.looperPreRoll;
          emit(FRONTEND_EVENTS.setLooperRecording, { enabled: !capturing });
        }
        return;
      }

      if (e.key === "Enter") {
        if (isControl(document.activeElement)) return;
        if (recordingMode === "take" && transport.takePending) {
          e.preventDefault();
          emit(FRONTEND_EVENTS.saveTake);
        }
        return;
      }

      if (e.key === "Escape") {
        if (Number(transport.playingSnippetId ?? -1) >= 0) emit(FRONTEND_EVENTS.stopPlayback);
        if (transport.takePlaying) emit(FRONTEND_EVENTS.setTakePlayback, { enabled: false });
      }
    };

    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, [
    recordingMode,
    transport.recording,
    transport.preRollActive,
    transport.looperRecording,
    transport.looperPreRoll,
    transport.takePending,
    transport.takePlaying,
    transport.playingSnippetId,
  ]);

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
      if (payload.input !== undefined) setInput(Number(payload.input));
      if (payload.output !== undefined) setOutput(Number(payload.output));
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
        recordLevel: Number(payload.recordLevel ?? prev.recordLevel ?? 0),
        recordPeak: Number(payload.recordPeak ?? prev.recordPeak ?? 0),
        outputLevel: Number(payload.outputLevel ?? prev.outputLevel ?? 0),
        outputPeak: Number(payload.outputPeak ?? prev.outputPeak ?? 0),
        loopPlayLevel: Number(payload.loopPlayLevel ?? prev.loopPlayLevel ?? 0),
        loopPlayPeak: Number(payload.loopPlayPeak ?? prev.loopPlayPeak ?? 0),
        inputClipped: payload.inputClipped !== undefined ? Boolean(payload.inputClipped) : prev.inputClipped,
        recordClipped: payload.recordClipped !== undefined ? Boolean(payload.recordClipped) : prev.recordClipped,
        outputClipped: payload.outputClipped !== undefined ? Boolean(payload.outputClipped) : prev.outputClipped,
        loopClipped: payload.loopClipped !== undefined ? Boolean(payload.loopClipped) : prev.loopClipped,
        libraryFolder: payload.libraryFolder ?? prev.libraryFolder,
        lastSaveError: payload.lastSaveError ?? prev.lastSaveError,
        metronomeEnabled: payload.metronomeEnabled !== undefined ? Boolean(payload.metronomeEnabled) : prev.metronomeEnabled,
        bpm:              payload.bpm !== undefined              ? Number(payload.bpm)              : prev.bpm,
        countInBeats:     payload.countInBeats !== undefined     ? Number(payload.countInBeats)     : prev.countInBeats,
        loopLevel:        payload.loopLevel !== undefined        ? Number(payload.loopLevel)        : prev.loopLevel,
        dryLevel:         payload.dryLevel !== undefined         ? Number(payload.dryLevel)         : prev.dryLevel,
        overdubLevel:     payload.overdubLevel !== undefined     ? Number(payload.overdubLevel)     : prev.overdubLevel,
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
         looperLengthBars: payload.looperLengthBars !== undefined ? Number(payload.looperLengthBars) : prev.looperLengthBars,
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
          monitorSolo: Boolean(base.monitorSolo),
          monitorMuted: Boolean(base.monitorMuted),
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

  const handleInputChange = (next) => {
    setInput(next);
    emit(FRONTEND_EVENTS.setParameter, { id: PARAM_IDS.input, value: next });
  };

  const handleOutputChange = (next) => {
    setOutput(next);
    emit(FRONTEND_EVENTS.setParameter, { id: PARAM_IDS.output, value: next });
  };

  const handleResetClip = (target) => {
    emit(FRONTEND_EVENTS.resetClip, { target });
    setTransport((prev) => {
      const next = { ...prev };
      if (target === "all") {
        for (const key of Object.values(CLIP_KEYS)) next[key] = false;
      } else if (CLIP_KEYS[target]) {
        next[CLIP_KEYS[target]] = false;
      }
      return next;
    });
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

  const handleLoopLevelChange = (next) => {
    setTransport((prev) => ({ ...prev, loopLevel: next }));
    emit(FRONTEND_EVENTS.setLoopLevel, { level: next });
  };

  const handleDryLevelChange = (next) => {
    setTransport((prev) => ({ ...prev, dryLevel: next }));
    emit(FRONTEND_EVENTS.setDryLevel, { level: next });
  };

  const handleOverdubLevelChange = (next) => {
    setTransport((prev) => ({ ...prev, overdubLevel: next }));
    emit(FRONTEND_EVENTS.setOverdubLevel, { level: next });
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

  // Restore ETA: the deferred restore is strictly serial (one slot per
  // message-loop turn), so a forward projection from observed throughput
  // is a fair "Xs left" while slots are still queued. When pendingTotal
  // hits 0 the last slot's saved state is being applied — a single
  // black-box operation with no progress source (C++ blocks inside the
  // plugin) — so the splash switches to an honest "applying saved state"
  // phase with a renderer-side elapsed clock instead of fake progress.
  const restoreStartedAt = useRef(null);
  useEffect(() => {
    if (vst3.restoring && restoreStartedAt.current === null)
      restoreStartedAt.current = Date.now();
    if (!vst3.restoring)
      restoreStartedAt.current = null;
  }, [vst3.restoring]);

  const restoredDone = Math.max(0, totalPlugins - pendingTotal);
  const etaSec = useMemo(() => {
    if (pendingTotal <= 0 || restoreStartedAt.current === null || restoredDone <= 0)
      return 0;
    const elapsedMs = Date.now() - restoreStartedAt.current;
    return Math.max(0, Math.round((elapsedMs / restoredDone) * pendingTotal / 1000));
  }, [pendingTotal, restoredDone, vst3.restoring]);

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
        <details className="shortcut-help">
          <summary title="Keyboard shortcuts">Keyboard</summary>
          <dl className="shortcut-list">
            <div><dt>Space</dt><dd>Start / stop capture (Take or Loop tab)</dd></div>
            <div><dt>Enter</dt><dd>Save the pending take</dd></div>
            <div><dt>Esc</dt><dd>Stop playback</dd></div>
          </dl>
        </details>
      </header>

      <section className="bp-section control-deck" aria-label="Monitoring controls">
        <div className="bp-section-head">
          <span className="section-index" aria-hidden="true">01</span>
          <h2 className="bp-section-title">Monitor</h2>
        </div>
        <div className="bp-section-body">
          <HeaderControls
            input={input}
            onInputChange={handleInputChange}
            output={output}
            onOutputChange={handleOutputChange}
            bpm={transport.bpm}
            onBpmChange={handleBpmChange}
            dryLevel={transport.dryLevel}
            onDryLevelChange={handleDryLevelChange}
            transport={transport}
            onResetClip={handleResetClip}
          />
        </div>
      </section>

      <section className="bp-section recording-section" aria-label="Recording">
        <div className="bp-section-head recording-section-head">
          <span className="section-index" aria-hidden="true">02</span>
          <h2 className="bp-section-title">Record</h2>
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
        </div>

        <div className="bp-section-body">
          {recordingMode === "take" ? (
            <div id="recording-panel-take" className="recording-panel" role="tabpanel" aria-labelledby="recording-tab-take">
              <Transport
                transport={transport}
                bpm={transport.bpm}
                countInBeats={transport.countInBeats}
                onCountInBeatsChange={handleCountInBeatsChange}
                onResetClip={handleResetClip}
              />

              <TakeReview transport={transport} />
            </div>
          ) : (
            <div id="recording-panel-loop" className="recording-panel" role="tabpanel" aria-labelledby="recording-tab-loop">
              <Looper
                transport={transport}
                onOverdubChange={handleLooperOverdubChange}
                onLoopLevelChange={handleLoopLevelChange}
                onOverdubLevelChange={handleOverdubLevelChange}
                onResetClip={handleResetClip}
              />
            </div>
          )}
        </div>
      </section>

      <PluginChain
        chainState={{ chains: vst3.chains, openEditors: vst3.openEditors }}
        inputChannels={vst3.inputChannels}
        chainLevels={transport.chainLevels}
        availablePlugins={vst3.available}
        defaultFolder={vst3.defaultFolder}
        scanState={scanState}
      />

      <section className="bp-section library-section" aria-label="Library">
        <div className="bp-section-head">
          <span className="section-index" aria-hidden="true">04</span>
          <h2 className="bp-section-title">Library</h2>
        </div>
        <div className="bp-section-body">
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
              folder={transport.libraryFolder}
            />
          </ErrorBoundary>
        </div>
      </section>

      {createPortal(
        <Notification
          notification={notification}
          onDismiss={() => setNotification(null)}
        />,
        document.body,
      )}

      {createPortal(
        <SplashScreen
          visible={splashPhase !== "hidden"}
          leaving={splashPhase === "leaving"}
          restoring={Boolean(vst3.restoring)}
          snapshotReceived={chainSnapshotReceived}
          progress={restoreProgress}
          remaining={pendingTotal}
          total={totalPlugins}
          etaSec={etaSec}
          chains={restoringChains}
          error={vst3.restoreError}
        />,
        document.body,
      )}

      <footer className="app-footer">
        <span className="app-footer-note">
          Takes stay in memory until you save or discard them — pick a library folder so saves have a destination.
        </span>
        <details className={`diagnostics-help ${vst3.restoreError ? "has-warning" : ""}`}>
          <summary title="Crash and restore diagnostics">Diagnostics</summary>
          <div className="diagnostics-panel" role="group" aria-label="Diagnostics">
            {vst3.restoreError ? (
              <p className="diagnostics-warning">Last chain restore: {vst3.restoreError}</p>
            ) : null}
            <div className="diagnostics-actions">
              <button
                type="button"
                className="btn btn-sm"
                onClick={() => emit(FRONTEND_EVENTS.copyDiagnostics)}
              >
                Copy diagnostics
              </button>
              <button
                type="button"
                className="btn btn-sm"
                onClick={() => emit(FRONTEND_EVENTS.openDiagnosticsFolder)}
              >
                Open folder
              </button>
            </div>
            <p className="diagnostics-hint">
              App and OS version, crash info, restore errors and the plugin quarantine — no audio
              or personal files. For full crash dumps, the copied report includes the elevated WER
              LocalDumps command.
            </p>
          </div>
        </details>
      </footer>
    </main>
  );
}
