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
  // Take-recorder overdub: { enabled } layers the next record over the
  // pending take instead of replacing it (session-only).
  setTakeOverdub: "frontendSetTakeOverdub",
  saveTake: "frontendSaveTake",
  discardTake: "frontendDiscardTake",
  startPlayback: "frontendStartPlayback",
  stopPlayback: "frontendStopPlayback",
  updateSnippet: "frontendUpdateSnippetMeta",
  // Organisational colour tag for a snippet: { id, color } where
  // color is one of the 8 palette keys or "" to clear.
  setSnippetColor: "frontendSetSnippetColor",
  // Non-destructive playback trim for a snippet: { id, gainDb } (-24..+24 dB).
  setSnippetGain: "frontendSetSnippetGain",
  // { id } — set the trim so the snippet peak lands at -1 dBFS.
  normalizeSnippet: "frontendNormalizeSnippet",
  deleteSnippet: "frontendDeleteSnippet",
  detectSnippetKey: "frontendDetectSnippetKey",
  saveSnippet: "frontendSaveSnippet",
  // { id } — export a snippet to a chosen file (WAV/AIFF/FLAC), baking the
  // non-destructive gainDb trim into the exported file.
  exportSnippet: "frontendExportSnippet",
  saveLoop: "frontendSaveLoop",
  revealSnippet: "frontendRevealSnippet",
  chooseLibraryFolder: "frontendChooseLibraryFolder",
  openLibraryFolder: "frontendOpenLibraryFolder",
  refreshLibrary: "frontendRefreshLibrary",
  // { } — copy an audio-free diagnostics report (versions, crash-info,
  // restore errors, quarantine) to the clipboard, or open the folder
  // that holds crash-info.txt / BluePrinter.properties.
  copyDiagnostics: "frontendCopyDiagnostics",
  openDiagnosticsFolder: "frontendOpenDiagnosticsFolder",
  // Import external audio: `importAudio` opens a multi-select file
  // chooser, `importAudioData { name, data }` imports one dropped file
  // whose bytes the WebView read (base64; no path is available).
  importAudio: "frontendImportAudio",
  importAudioData: "frontendImportAudioData",
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
  // Monitor-only playback level for the looper in dB (-60..+12).
  // { level }
  setLoopLevel: "frontendSetLoopLevel",
  // Direct dry pass-through in dB (-60..0, 0 = unity), applied to both
  // the monitor and the capture. { level }
  setDryLevel: "frontendSetDryLevel",
  // Overdub trim in dB (-60..0) applied to each new layer before it is
  // mixed into the loop. { level }
  setOverdubLevel: "frontendSetOverdubLevel",
  // Clear a latched clip indicator: { target } is one of
  // "input" | "record" | "output" | "loop" | "all".
  resetClip: "frontendResetClip",
  // Click sound tuning popup: { pitch, accentPitch, decay, volume,
  // accentVolume, noise } — all values sent on every change.
  setClickParams: "frontendSetClickParams",
  setMidiClock: "frontendSetMidiClock",
  // { enabled } — when on (and the MIDI clock toggle is on), the clock
  // doesn't free-run: it starts when a take or loop capture begins
  // (count-in included) and stops when it ends.
  setMidiClockOnRecord: "frontendSetMidiClockOnRecord",
  setMidiDevice: "frontendSetMidiDevice",
  // Header-level click-during-capture gate: { enabled } — false = the
  // click only plays during count-ins, silent through takes and loop
  // captures. Shared by the take recorder and the looper.
  setClickDuringCapture: "frontendSetClickDuringCapture",
  setLooperRecording: "frontendSetLooperRecording",
  setLooperPlaying: "frontendSetLooperPlaying",
  setLooperLooping: "frontendSetLooperLooping",
  // { enabled } — overdub mode: with a loop captured and looping on,
  // record layers the new input over the loop instead of replacing it.
  setLooperOverdub: "frontendSetLooperOverdub",
  setLooperCountIn: "frontendSetLooperCountIn",
  // Fixed capture length: { bars } — 0 = Free, else the capture auto-stops
  // after exactly that many bars (1/2/4/8).
  setLooperLengthBars: "frontendSetLooperLengthBars",
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
  // Monitor-only solo/mute: { chain, solo } / { chain, muted }. These
  // change what is heard, never the capture.
  setChainMonitorSolo: "frontendSetChainMonitorSolo",
  setChainMonitorMute: "frontendSetChainMonitorMute",
  setChainMidiChannels: "frontendSetChainMidiChannels",
  // { dWidth } — device-pixel width delta from the corner resize grip.
  resizeEditor: "frontendResizeEditor",
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
