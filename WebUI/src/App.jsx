import { useEffect, useState } from "react";
import { Knob } from "./components/controls";

const FRONTEND_EVENT = "frontendSetParameter";
const BACKEND_EVENT = "backendParameters";

const PARAM_IDS = {
  gain: "Gain",
};

function getBackend() {
  return window.__JUCE__?.backend;
}

function emitParameterChange(id, value) {
  const backend = getBackend();
  if (!backend?.emitEvent) return;
  backend.emitEvent(FRONTEND_EVENT, { id, value });
}

function readInitialParametersFromJuce() {
  const first = window.__JUCE__?.initialisationData?.parameters?.[0];
  if (!first) return null;
  return { gain: Number(first.gain ?? 0.5) };
}

export default function App() {
  const initial = readInitialParametersFromJuce();
  const [gain, setGain] = useState(initial?.gain ?? 0.5);

  useEffect(() => {
    const backend = getBackend();
    if (!backend?.addEventListener) return undefined;

    const token = backend.addEventListener(BACKEND_EVENT, (payload) => {
      if (typeof payload !== "object" || payload == null) return;
      if (payload.gain !== undefined) setGain(Number(payload.gain));
    });

    return () => {
      if (backend.removeEventListener && token !== undefined) {
        backend.removeEventListener(token);
      }
    };
  }, []);

  return (
    <main className="app">
      <header className="app-header">
        <h1>BluePrinter Template</h1>
        <p>JUCE + React + WebView2 bridge example</p>
      </header>
      <section className="app-body">
        <Knob
          label="Gain"
          min={0}
          max={1}
          value={gain}
          onChange={(next) => {
            setGain(next);
            emitParameterChange(PARAM_IDS.gain, next);
          }}
          decimals={2}
        />
      </section>
      <footer className="app-footer">
        <span>Edit WebUI/src/App.jsx to build your own UI.</span>
      </footer>
    </main>
  );
}
