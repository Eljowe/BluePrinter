// Bridge helpers — every interaction with the JUCE backend goes through here.
//
// Chain events carry a "chain" field that is either "midiChain" or
// "audioChain", picking which of the two parallel plugin chains the
// operation targets. The MIDI chain runs first in the audio thread
// (good for arpeggiators / chord generators / instruments) and the
// audio chain runs second (good for amp sims / EQ / reverb). See
// PluginChain.h and PluginProcessor.h for the details.

export const CHAIN_IDS = {
  midi: "midiChain",
  audio: "audioChain",
};

export const FRONTEND_EVENTS = {
  setParameter: "frontendSetParameter",
  startRecording: "frontendStartRecording",
  stopRecording: "frontendStopRecording",
  startPlayback: "frontendStartPlayback",
  stopPlayback: "frontendStopPlayback",
  updateSnippet: "frontendUpdateSnippetMeta",
  deleteSnippet: "frontendDeleteSnippet",
  detectSnippetKey: "frontendDetectSnippetKey",
  saveSnippet: "frontendSaveSnippet",
  revealSnippet: "frontendRevealSnippet",
  chooseLibraryFolder: "frontendChooseLibraryFolder",
  openLibraryFolder: "frontendOpenLibraryFolder",
  refreshLibrary: "frontendRefreshLibrary",
  // Request a fresh snippet snapshot from the backend. Fired once
  // when the React app mounts, because the snippets loaded from
  // disk in the processor's constructor arrive before the editor
  // listener is registered, so the notification is lost.
  getSnippets: "frontendGetSnippets",
  setMetronome: "frontendSetMetronome",
  setBpm: "frontendSetBpm",
  setCountInBeats: "frontendSetCountInBeats",
  addVst3: "frontendAddVst3",
  removeVst3: "frontendRemoveVst3",
  moveVst3: "frontendMoveVst3",
  setVst3Bypass: "frontendSetVst3Bypass",
  openVst3Editor: "frontendOpenVst3Editor",
  closeVst3Editor: "frontendCloseVst3Editor",
  scanVst3Folder: "frontendScanVst3Folder",
  getVst3Chain: "frontendGetVst3Chain",
  blockVst3Plugin: "frontendBlockVst3Plugin",
  unblockVst3Plugin: "frontendUnblockVst3Plugin",
};

export const BACKEND_EVENTS = {
  parameters: "backendParameters",
  snippets: "backendSnippets",
  transport: "backendTransport",
  notify: "backendNotify",
  vst3Chain: "backendVst3Chain",
  vst3ScanProgress: "backendVst3ScanProgress",
  vst3LoadFailed: "backendVst3LoadFailed",
};

export function getBackend() {
  return window.__JUCE__?.backend;
}

export function getInitialData() {
  return window.__JUCE__?.initialisationData ?? {};
}

export function emit(event, data = {}) {
  const backend = getBackend();
  if (!backend?.emitEvent) return;
  backend.emitEvent(event, data);
}

export function subscribe(event, callback) {
  const backend = getBackend();
  if (!backend?.addEventListener) return () => {};

  const token = backend.addEventListener(event, (payload) => {
    try {
      callback(payload);
    } catch (err) {
      console.error("[BluePrinter] listener error for", event, err);
    }
  });

  return () => {
    if (backend.removeEventListener && token !== undefined) {
      backend.removeEventListener(token);
    }
  };
}
