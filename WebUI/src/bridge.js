// Bridge helpers — every interaction with the JUCE backend goes through here.

export const FRONTEND_EVENTS = {
  setParameter: "frontendSetParameter",
  startRecording: "frontendStartRecording",
  stopRecording: "frontendStopRecording",
  startPlayback: "frontendStartPlayback",
  stopPlayback: "frontendStopPlayback",
  updateSnippet: "frontendUpdateSnippetMeta",
  deleteSnippet: "frontendDeleteSnippet",
  saveSnippet: "frontendSaveSnippet",
  revealSnippet: "frontendRevealSnippet",
  chooseLibraryFolder: "frontendChooseLibraryFolder",
  openLibraryFolder: "frontendOpenLibraryFolder",
  refreshLibrary: "frontendRefreshLibrary",
  setMetronome: "frontendSetMetronome",
  setBpm: "frontendSetBpm",
  setCountInBeats: "frontendSetCountInBeats",
};

export const BACKEND_EVENTS = {
  parameters: "backendParameters",
  snippets: "backendSnippets",
  transport: "backendTransport",
  notify: "backendNotify",
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
