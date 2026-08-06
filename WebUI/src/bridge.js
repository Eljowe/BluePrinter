// Bridge helpers — every interaction with the JUCE backend goes through here.
//
// Chain events carry a "chain" field that is a stable chain id
// ("chain0", "chain1", …). Chains are independent parallel processors
// of the input: each selects which input channels feed it (none /
// left / right / both), whether it receives the MIDI buffer, and
// whether its output is included in take/loop captures. See
// PluginChain.h and PluginProcessor.h for the details.

export const FRONTEND_EVENTS = {
  setParameter: "frontendSetParameter",
  startRecording: "frontendStartRecording",
  stopRecording: "frontendStopRecording",
  // Pending-take review: { enabled } toggles take playback; saveTake /
  // discardTake finalise the recorded take after review.
  setTakePlayback: "frontendSetTakePlayback",
  saveTake: "frontendSaveTake",
  discardTake: "frontendDiscardTake",
  startPlayback: "frontendStartPlayback",
  stopPlayback: "frontendStopPlayback",
  updateSnippet: "frontendUpdateSnippetMeta",
  // Organisational colour tag for a snippet: { id, color } where
  // color is one of the 8 palette keys or "" to clear.
  setSnippetColor: "frontendSetSnippetColor",
  deleteSnippet: "frontendDeleteSnippet",
  detectSnippetKey: "frontendDetectSnippetKey",
  saveSnippet: "frontendSaveSnippet",
  saveLoop: "frontendSaveLoop",
  revealSnippet: "frontendRevealSnippet",
  chooseLibraryFolder: "frontendChooseLibraryFolder",
  openLibraryFolder: "frontendOpenLibraryFolder",
  refreshLibrary: "frontendRefreshLibrary",
  // Request a fresh snippet snapshot from the backend. Fired once
  // when the React app mounts, because the snippets loaded from
  // disk in the processor's constructor arrive before the editor
  // listener is registered, so the notification is lost.
  getSnippets: "frontendGetSnippets",
  // { color, name } — a user name for a snippet colour tag (empty
  // name resets to the built-in label). Persisted in the properties
  // file, shipped back with every library snapshot as `tagNames`.
  renameTag: "frontendRenameTag",
  setMetronome: "frontendSetMetronome",
  setBpm: "frontendSetBpm",
  setCountInBeats: "frontendSetCountInBeats",
  // Level of the direct dry pass-through (0..1). { level }
  setDryLevel: "frontendSetDryLevel",
  // Click sound tuning popup: { pitch, accentPitch, decay, volume,
  // accentVolume, noise } — all values sent on every change.
  setClickParams: "frontendSetClickParams",
  setMidiClock: "frontendSetMidiClock",
  setMidiDevice: "frontendSetMidiDevice",
  // Header-level click-during-capture gate: { enabled } — false = the
  // click only plays during count-ins, silent through takes and loop
  // captures. Shared by the take recorder and the looper.
  setClickDuringCapture: "frontendSetClickDuringCapture",
  setLooperRecording: "frontendSetLooperRecording",
  setLooperPlaying: "frontendSetLooperPlaying",
  setLooperLooping: "frontendSetLooperLooping",
  setLooperCountIn: "frontendSetLooperCountIn",
  // Crop the captured loop: { startBeats, endBeats } in whole beats
  // (4 per bar at the current BPM).
  setLoopCrop: "frontendSetLoopCrop",
  clearLoop: "frontendClearLoop",
  addVst3: "frontendAddVst3",
  removeVst3: "frontendRemoveVst3",
  moveVst3: "frontendMoveVst3",
  setVst3Bypass: "frontendSetVst3Bypass",
  setVst3MidiPass: "frontendSetVst3MidiPass",
  openVst3Editor: "frontendOpenVst3Editor",
  closeVst3Editor: "frontendCloseVst3Editor",
  scanVst3Folder: "frontendScanVst3Folder",
  getVst3Chain: "frontendGetVst3Chain",
  blockVst3Plugin: "frontendBlockVst3Plugin",
  unblockVst3Plugin: "frontendUnblockVst3Plugin",
  // Chain lifecycle. "chain" fields are chain ids; addChain takes
  // { name?, inputs?: [0..7], wantsMidi?, recordOnCapture? }.
  addChain: "frontendAddChain",
  removeChain: "frontendRemoveChain",
  renameChain: "frontendRenameChain",
  setChainInputs: "frontendSetChainInputs",
  setChainRecord: "frontendSetChainRecord",
  // Chain mix controls: volume in dB (-60..+12), mute, and the MIDI
  // channel filter (channels: [1..16]).
  setChainVolume: "frontendSetChainVolume",
  setChainMute: "frontendSetChainMute",
  setChainMidiChannels: "frontendSetChainMidiChannels",
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
