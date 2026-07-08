import { useEffect, useMemo, useState } from "react";
import { Knob } from "./components/controls";
import EqPanel from "./eq/EqPanel";
import Pedalboard from "./pedals/Pedalboard";

const PARAM_IDS = {
  input: "Input Gain",
  tunerBypassed: "Tuner Bypassed",
  gateThreshold: "Gate Threshold",
  gateBypassed: "Gate Bypassed",
  compressorAmount: "Compressor Amount",
  compressorTone: "Compressor Tone",
  compressorLevel: "Compressor Level",
  octaveTranspose: "Octave Transpose",
  octaveBypassed: "Octave Bypassed",
  doublerMix: "Doubler Mix",
  doublerDelay: "Doubler Delay",
  doublerDetune: "Doubler Detune",
  doublerBypassed: "Doubler Bypassed",
  tremoloSpeed: "Tremolo Speed",
  tremoloDepth: "Tremolo Depth",
  tremoloLfo: "Tremolo LFO",
  tremoloBypassed: "Tremolo Bypassed",
  delayMix: "Delay Mix",
  delayTimeL: "Delay Time L",
  delayTimeR: "Delay Time R",
  delayFeedback: "Delay Feedback",
  delayModeIsDual: "Delay Mode",
  delayBypassed: "Delay Bypassed",
  monoInput: "Mono Input",
  drive: "Distortion Drive",
  driveTone: "Distortion Tone",
  driveLevel: "Distortion Level",
  fuzzDrive: "Fuzz Drive",
  fuzzTone: "Fuzz Tone",
  fuzzLevel: "Fuzz Level",
  output: "Output Gain",
  mute: "Mute",
  eqBands: Array.from({ length: 10 }, (_, i) => `EQ Band ${i}`),
  eqBypassed: "EQ Bypassed",
  driveBypassed: "Distortion Bypassed",
  fuzzBypassed: "Fuzz Bypassed",
  compressorBypassed: "Compressor Bypassed",
  reverbSize: "Reverb Size",
  reverbDamping: "Reverb Damping",
  reverbMix: "Reverb Mix",
  reverbWidth: "Reverb Width",
  reverbBypassed: "Reverb Bypassed",
  analyzerEnabled: "Analyzer Enabled",
};

const FRONTEND_EVENT = "frontendSetParameter";
const BACKEND_EVENT = "backendParameters";

const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

function noteIndexToName(noteIndex) {
  if (noteIndex == null || noteIndex < 0) return "";
  const pitchClass = ((noteIndex % 12) + 12) % 12;
  const octave = Math.floor(noteIndex / 12) - 1;
  return `${NOTE_NAMES[pitchClass]}${octave}`;
}

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

  return {
    inputGain: Number(first.inputGain ?? 0),
    tunerReference: Number(first.tunerReference ?? 440),
    tunerBypassed: Boolean(first.tunerBypassed),
    gateThreshold: Number(first.gateThreshold ?? -60),
    gateBypassed: Boolean(first.gateBypassed),
    compressorAmount: Number(first.compressorAmount ?? 0),
    compressorTone: Number(first.compressorTone ?? 0),
    compressorLevel: Number(first.compressorLevel ?? 0),
    octaveTranspose: Number(first.octaveTranspose ?? 0),
    doublerMix: Number(first.doublerMix ?? 0),
    doublerDelay: Number(first.doublerDelay ?? 20),
    doublerDetune: Number(first.doublerDetune ?? 5),
    tremoloSpeed: Number(first.tremoloSpeed ?? 5),
    tremoloDepth: Number(first.tremoloDepth ?? 0.5),
    tremoloLfoIndex: Number(first.tremoloLfoIndex ?? 0),
    delayMix: Number(first.delayMix ?? 0.35),
    delayTimeL: Number(first.delayTimeL ?? 350),
    delayTimeR: Number(first.delayTimeR ?? 350),
    delayFeedback: Number(first.delayFeedback ?? 0.35),
    delayModeIsDual: Boolean(first.delayModeIsDual),
    monoInput: Boolean(first.monoInput),
    drive: Number(first.drive ?? 6),
    driveTone: Number(first.driveTone ?? 0.7),
    driveLevel: Number(first.driveLevel ?? 0),
    fuzzDrive: Number(first.fuzzDrive ?? 0),
    fuzzTone: Number(first.fuzzTone ?? 0.7),
    fuzzLevel: Number(first.fuzzLevel ?? 0),
    outputGain: Number(first.outputGain ?? 0),
    mute: Boolean(first.mute),
    eqBands: Array.isArray(first.eqBands) ? first.eqBands.map((v) => Number(v)) : Array(10).fill(0),
    eqBypassed: Boolean(first.eqBypassed),
    reverbSize: Number(first.reverbSize ?? 0.5),
    reverbDamping: Number(first.reverbDamping ?? 0.5),
    reverbMix: Number(first.reverbMix ?? 0),
    reverbWidth: Number(first.reverbWidth ?? 1),
    highCutBypassed: Boolean(first.highCutBypassed),
    driveBypassed: Boolean(first.driveBypassed),
    fuzzBypassed: Boolean(first.fuzzBypassed),
    compressorBypassed: Boolean(first.compressorBypassed),
    octaveBypassed: Boolean(first.octaveBypassed),
    doublerBypassed: Boolean(first.doublerBypassed),
    delayBypassed: Boolean(first.delayBypassed),
    reverbBypassed: Boolean(first.reverbBypassed),
    responseCurve: Array.isArray(first.responseCurve) ? first.responseCurve.map(Number) : [],
    leftSpectrum: Array.isArray(first.leftSpectrum) ? first.leftSpectrum.map(Number) : [],
    rightSpectrum: Array.isArray(first.rightSpectrum) ? first.rightSpectrum.map(Number) : [],
    analyzerEnabled: Boolean(first.analyzerEnabled),
    inputPeak: Number(first.inputPeak ?? 0),
    outputPeak: Number(first.outputPeak ?? 0),
    inputClipping: Boolean(first.inputClipping),
    outputClipping: Boolean(first.outputClipping),
  };
}

function MiniPeak({ label, level, clipping, className = "" }) {
  const height = `${Math.min(100, Math.max(0, level * 100))}%`;

  return (
    <div className={`mini-peak ${className}`.trim()}>
      <div className="mini-peak-head">
        <span>{label}</span>
        <span className={`mini-clip ${clipping ? "active" : ""}`}>{clipping ? "CLIP" : "OK"}</span>
      </div>
      <div className="mini-peak-track">
        <div className="mini-peak-fill" style={{ height }} />
      </div>
    </div>
  );
}

export default function App() {
  const initial = useMemo(() => readInitialParametersFromJuce(), []);

  const [activeTab, setActiveTab] = useState("fx");
  const [inGain, setInGain] = useState(initial?.inputGain ?? 0);
  const [tunerBypassed, setTunerBypassed] = useState(initial?.tunerBypassed ?? false);
  const [gateThreshold, setGateThreshold] = useState(initial?.gateThreshold ?? -60);
  const [gateBypassed, setGateBypassed] = useState(initial?.gateBypassed ?? false);
  const [tunerNoteIndex, setTunerNoteIndex] = useState(-1);
  const [tunerCents, setTunerCents] = useState(0);
  const [tunerFrequency, setTunerFrequency] = useState(0);
  const [tunerLevel, setTunerLevel] = useState(-60);

  const tunerNote = tunerNoteIndex >= 0 ? noteIndexToName(tunerNoteIndex) : "";
  const [drive, setDrive] = useState(initial?.drive ?? 6);
  const [driveTone, setDriveTone] = useState(initial?.driveTone ?? 0.7);
  const [driveLevel, setDriveLevel] = useState(initial?.driveLevel ?? 0);
  const [fuzzDrive, setFuzzDrive] = useState(initial?.fuzzDrive ?? 0);
  const [fuzzTone, setFuzzTone] = useState(initial?.fuzzTone ?? 0.7);
  const [fuzzLevel, setFuzzLevel] = useState(initial?.fuzzLevel ?? 0);
  const [compressorAmount, setCompressorAmount] = useState(initial?.compressorAmount ?? 0);
  const [compressorTone, setCompressorTone] = useState(initial?.compressorTone ?? 0);
  const [compressorLevel, setCompressorLevel] = useState(initial?.compressorLevel ?? 0);
  const [octaveTranspose, setOctaveTranspose] = useState(initial?.octaveTranspose ?? 0);
  const [doublerMix, setDoublerMix] = useState(initial?.doublerMix ?? 0);
  const [doublerDelay, setDoublerDelay] = useState(initial?.doublerDelay ?? 20);
  const [doublerDetune, setDoublerDetune] = useState(initial?.doublerDetune ?? 5);
  const [tremoloSpeed, setTremoloSpeed] = useState(initial?.tremoloSpeed ?? 5);
  const [tremoloDepth, setTremoloDepth] = useState(initial?.tremoloDepth ?? 0.5);
  const [tremoloLfoIndex, setTremoloLfoIndex] = useState(initial?.tremoloLfoIndex ?? 0);
  const [delayMix, setDelayMix] = useState(initial?.delayMix ?? 0.35);
  const [delayTimeL, setDelayTimeL] = useState(initial?.delayTimeL ?? 350);
  const [delayTimeR, setDelayTimeR] = useState(initial?.delayTimeR ?? 350);
  const [delayFeedback, setDelayFeedback] = useState(initial?.delayFeedback ?? 0.35);
  const [delayModeIsDual, setDelayModeIsDual] = useState(initial?.delayModeIsDual ?? false);
  const [outGain, setOutGain] = useState(initial?.outputGain ?? 0);
  const [monoInput, setMonoInput] = useState(initial?.monoInput ?? false);
  const [mute, setMute] = useState(initial?.mute ?? false);
  const [eqBands, setEqBands] = useState(Array.isArray(initial?.eqBands) ? initial.eqBands : Array(10).fill(0));
  const [eqBypassed, setEqBypassed] = useState(initial?.eqBypassed ?? false);
  const [driveBypassed, setDriveBypassed] = useState(initial?.driveBypassed ?? false);
  const [fuzzBypassed, setFuzzBypassed] = useState(initial?.fuzzBypassed ?? false);
  const [compressorBypassed, setCompressorBypassed] = useState(initial?.compressorBypassed ?? false);
  const [octaveBypassed, setOctaveBypassed] = useState(initial?.octaveBypassed ?? false);
  const [doublerBypassed, setDoublerBypassed] = useState(initial?.doublerBypassed ?? false);
  const [tremoloBypassed, setTremoloBypassed] = useState(initial?.tremoloBypassed ?? false);
  const [delayBypassed, setDelayBypassed] = useState(initial?.delayBypassed ?? false);
  const [reverbSize, setReverbSize] = useState(initial?.reverbSize ?? 0.5);
  const [reverbDamping, setReverbDamping] = useState(initial?.reverbDamping ?? 0.5);
  const [reverbMix, setReverbMix] = useState(initial?.reverbMix ?? 0);
  const [reverbWidth, setReverbWidth] = useState(initial?.reverbWidth ?? 1);
  const [reverbBypassed, setReverbBypassed] = useState(initial?.reverbBypassed ?? false);
  const [responseCurve, setResponseCurve] = useState(initial?.responseCurve ?? []);
  const [leftSpectrum, setLeftSpectrum] = useState(initial?.leftSpectrum ?? []);
  const [rightSpectrum, setRightSpectrum] = useState(initial?.rightSpectrum ?? []);
  const [analyzerEnabled, setAnalyzerEnabled] = useState(initial?.analyzerEnabled ?? true);
  const [inputPeak, setInputPeak] = useState(initial?.inputPeak ?? 0);
  const [outputPeak, setOutputPeak] = useState(initial?.outputPeak ?? 0);
  const [inputClipping, setInputClipping] = useState(initial?.inputClipping ?? false);
  const [outputClipping, setOutputClipping] = useState(initial?.outputClipping ?? false);

  useEffect(() => {
    const backend = getBackend();
    if (!backend?.addEventListener) return undefined;

    const token = backend.addEventListener(BACKEND_EVENT, (payload) => {
      if (typeof payload !== "object" || payload == null) return;
      if (payload.inputGain !== undefined) setInGain(Number(payload.inputGain));
      if (payload.tunerBypassed !== undefined) setTunerBypassed(Boolean(payload.tunerBypassed));
      if (payload.gateThreshold !== undefined) setGateThreshold(Number(payload.gateThreshold));
      if (payload.gateBypassed !== undefined) setGateBypassed(Boolean(payload.gateBypassed));
      if (payload.tuner && typeof payload.tuner === "object") {
        if (payload.tuner.frequency !== undefined) setTunerFrequency(Number(payload.tuner.frequency));
        if (payload.tuner.cents !== undefined) setTunerCents(Number(payload.tuner.cents));
        if (payload.tuner.noteIndex !== undefined) setTunerNoteIndex(Number(payload.tuner.noteIndex));
        if (payload.tuner.level !== undefined) setTunerLevel(Number(payload.tuner.level));
      }
      if (payload.compressorAmount !== undefined) setCompressorAmount(Number(payload.compressorAmount));
      if (payload.compressorTone !== undefined) setCompressorTone(Number(payload.compressorTone));
      if (payload.compressorLevel !== undefined) setCompressorLevel(Number(payload.compressorLevel));
      if (payload.octaveTranspose !== undefined) setOctaveTranspose(Number(payload.octaveTranspose));
      if (payload.doublerMix !== undefined) setDoublerMix(Number(payload.doublerMix));
      if (payload.doublerDelay !== undefined) setDoublerDelay(Number(payload.doublerDelay));
      if (payload.doublerDetune !== undefined) setDoublerDetune(Number(payload.doublerDetune));
      if (payload.tremoloSpeed !== undefined) setTremoloSpeed(Number(payload.tremoloSpeed));
      if (payload.tremoloDepth !== undefined) setTremoloDepth(Number(payload.tremoloDepth));
      if (payload.tremoloLfoIndex !== undefined) setTremoloLfoIndex(Number(payload.tremoloLfoIndex));
      if (payload.delayMix !== undefined) setDelayMix(Number(payload.delayMix));
      if (payload.delayTimeL !== undefined) setDelayTimeL(Number(payload.delayTimeL));
      if (payload.delayTimeR !== undefined) setDelayTimeR(Number(payload.delayTimeR));
      if (payload.delayFeedback !== undefined) setDelayFeedback(Number(payload.delayFeedback));
      if (payload.delayModeIsDual !== undefined) setDelayModeIsDual(Boolean(payload.delayModeIsDual));
      if (payload.monoInput !== undefined) setMonoInput(Boolean(payload.monoInput));
      if (payload.drive !== undefined) setDrive(Number(payload.drive));
      if (payload.driveTone !== undefined) setDriveTone(Number(payload.driveTone));
      if (payload.driveLevel !== undefined) setDriveLevel(Number(payload.driveLevel));
      if (payload.fuzzDrive !== undefined) setFuzzDrive(Number(payload.fuzzDrive));
      if (payload.fuzzTone !== undefined) setFuzzTone(Number(payload.fuzzTone));
      if (payload.fuzzLevel !== undefined) setFuzzLevel(Number(payload.fuzzLevel));
      if (payload.outputGain !== undefined) setOutGain(Number(payload.outputGain));
      if (payload.monoInput !== undefined) setMonoInput(Boolean(payload.monoInput));
      if (payload.mute !== undefined) setMute(Boolean(payload.mute));
      if (Array.isArray(payload.eqBands)) {
        setEqBands(payload.eqBands.map((v) => Number(v)));
      }
      if (payload.eqBypassed !== undefined) setEqBypassed(Boolean(payload.eqBypassed));
      if (payload.driveBypassed !== undefined) setDriveBypassed(Boolean(payload.driveBypassed));
      if (payload.fuzzBypassed !== undefined) setFuzzBypassed(Boolean(payload.fuzzBypassed));
      if (payload.compressorBypassed !== undefined) setCompressorBypassed(Boolean(payload.compressorBypassed));
      if (payload.octaveBypassed !== undefined) setOctaveBypassed(Boolean(payload.octaveBypassed));
      if (payload.doublerBypassed !== undefined) setDoublerBypassed(Boolean(payload.doublerBypassed));
      if (payload.tremoloBypassed !== undefined) setTremoloBypassed(Boolean(payload.tremoloBypassed));
      if (payload.delayBypassed !== undefined) setDelayBypassed(Boolean(payload.delayBypassed));
      if (payload.reverbSize !== undefined) setReverbSize(Number(payload.reverbSize));
      if (payload.reverbDamping !== undefined) setReverbDamping(Number(payload.reverbDamping));
      if (payload.reverbMix !== undefined) setReverbMix(Number(payload.reverbMix));
      if (payload.reverbWidth !== undefined) setReverbWidth(Number(payload.reverbWidth));
      if (payload.reverbBypassed !== undefined) setReverbBypassed(Boolean(payload.reverbBypassed));
      if (payload.analyzerEnabled !== undefined) setAnalyzerEnabled(Boolean(payload.analyzerEnabled));
      if (Array.isArray(payload.responseCurve)) setResponseCurve(payload.responseCurve.map(Number));
      if (Array.isArray(payload.leftSpectrum)) setLeftSpectrum(payload.leftSpectrum.map(Number));
      if (Array.isArray(payload.rightSpectrum)) setRightSpectrum(payload.rightSpectrum.map(Number));
      if (payload.inputPeak !== undefined) setInputPeak(Number(payload.inputPeak));
      if (payload.outputPeak !== undefined) setOutputPeak(Number(payload.outputPeak));
      if (payload.inputClipping !== undefined) setInputClipping(Boolean(payload.inputClipping));
      if (payload.outputClipping !== undefined) setOutputClipping(Boolean(payload.outputClipping));
    });

    return () => {
      if (backend.removeEventListener && token !== undefined) {
        backend.removeEventListener(token);
      }
    };
  }, []);

  return (
    <main className="app">
      <header className="chrome-header">
        <div className="brand-block">
          <span className="brand-mark">A</span>
          <div>
            <p className="brand-name">IRONFORGE</p>
            <p className="brand-version">v2.4.1</p>
          </div>
        </div>

        <nav className="tab-row" aria-label="Editor sections">
          <button
            className={`tab-button ${activeTab === "fx" ? "active" : ""}`}
            onClick={() => setActiveTab("fx")}
            type="button"
          >
            FX
          </button>
          <button
            className={`tab-button ${activeTab === "eq" ? "active" : ""}`}
            onClick={() => setActiveTab("eq")}
            type="button"
          >
            EQ
          </button>
        </nav>

        <div className="io-dock">
          <div className="io-row">
            <div className="io-mode-toggle">
              <button
                type="button"
                className={`io-mode-segment ${monoInput ? "active" : ""}`.trim()}
                onClick={() => {
                  if (!monoInput) emitParameterChange(PARAM_IDS.monoInput, 1);
                }}
                aria-label="Mono input"
                title="Mono Input"
              >
                M
              </button>
              <button
                type="button"
                className={`io-mode-segment ${!monoInput ? "active" : ""}`.trim()}
                onClick={() => {
                  if (monoInput) emitParameterChange(PARAM_IDS.monoInput, 0);
                }}
                aria-label="Stereo input"
                title="Stereo Input"
              >
                S
              </button>
            </div>
            <button
              type="button"
              className={`io-mute-button ${mute ? "active" : ""}`.trim()}
              onClick={() => {
                const next = !mute;
                setMute(next);
                emitParameterChange(PARAM_IDS.mute, next ? 1 : 0);
              }}
              aria-label={mute ? "Unmute" : "Mute"}
              title={mute ? "Muted" : "Mute"}
            >
              M
            </button>
            <MiniPeak className="io-meter" label="Input" level={inputPeak} clipping={inputClipping} />
            <Knob
              className="io-compact"
              label="Input"
              min={-24}
              max={24}
              value={inGain}
              onChange={(next) => {
                setInGain(next);
                emitParameterChange(PARAM_IDS.input, next);
              }}
              unit="dB"
              accent="orange"
            />
            <Knob
              className="io-compact"
              label="Output"
              min={-24}
              max={24}
              value={outGain}
              onChange={(next) => {
                setOutGain(next);
                emitParameterChange(PARAM_IDS.output, next);
              }}
              unit="dB"
              accent="yellow"
            />
            <MiniPeak className="io-meter" label="Output" level={outputPeak} clipping={outputClipping} />
          </div>
        </div>
      </header>

      {activeTab === "fx" ? (
        <Pedalboard
          tunerBypassed={tunerBypassed}
          tunerNote={tunerNote}
          tunerCents={tunerCents}
          tunerFrequency={tunerFrequency}
          tunerLevel={tunerLevel}
          gateThreshold={gateThreshold}
          gateBypassed={gateBypassed}
          drive={drive}
          driveTone={driveTone}
          driveLevel={driveLevel}
          driveBypassed={driveBypassed}
          fuzzDrive={fuzzDrive}
          fuzzTone={fuzzTone}
          fuzzLevel={fuzzLevel}
          fuzzBypassed={fuzzBypassed}
          compressorAmount={compressorAmount}
          compressorBypassed={compressorBypassed}
          compressorTone={compressorTone}
          compressorLevel={compressorLevel}
          octaveTranspose={octaveTranspose}
          octaveBypassed={octaveBypassed}
          doublerMix={doublerMix}
          doublerDelay={doublerDelay}
          doublerDetune={doublerDetune}
          doublerBypassed={doublerBypassed}
          tremoloSpeed={tremoloSpeed}
          tremoloDepth={tremoloDepth}
          tremoloLfoIndex={tremoloLfoIndex}
          tremoloBypassed={tremoloBypassed}
          delayMix={delayMix}
          delayTimeL={delayTimeL}
          delayTimeR={delayTimeR}
          delayFeedback={delayFeedback}
          delayModeIsDual={delayModeIsDual}
          delayBypassed={delayBypassed}
          reverbSize={reverbSize}
          reverbDamping={reverbDamping}
          reverbMix={reverbMix}
          reverbWidth={reverbWidth}
          reverbBypassed={reverbBypassed}
          onTunerToggle={() => {
            const next = !tunerBypassed;
            setTunerBypassed(next);
            emitParameterChange(PARAM_IDS.tunerBypassed, next ? 1 : 0);
          }}
          onGateThresholdChange={(next) => {
            setGateThreshold(next);
            emitParameterChange(PARAM_IDS.gateThreshold, next);
          }}
          onGateToggle={() => {
            const next = !gateBypassed;
            setGateBypassed(next);
            emitParameterChange(PARAM_IDS.gateBypassed, next ? 1 : 0);
          }}
          onDriveChange={(next) => {
            setDrive(next);
            emitParameterChange(PARAM_IDS.drive, next);
          }}
          onDriveToneChange={(next) => {
            setDriveTone(next);
            emitParameterChange(PARAM_IDS.driveTone, next);
          }}
          onDriveLevelChange={(next) => {
            setDriveLevel(next);
            emitParameterChange(PARAM_IDS.driveLevel, next);
          }}
          onDriveToggle={() => {
            const next = !driveBypassed;
            setDriveBypassed(next);
            emitParameterChange(PARAM_IDS.driveBypassed, next ? 1 : 0);
          }}
          onFuzzDriveChange={(next) => {
            setFuzzDrive(next);
            emitParameterChange(PARAM_IDS.fuzzDrive, next);
          }}
          onFuzzToneChange={(next) => {
            setFuzzTone(next);
            emitParameterChange(PARAM_IDS.fuzzTone, next);
          }}
          onFuzzLevelChange={(next) => {
            setFuzzLevel(next);
            emitParameterChange(PARAM_IDS.fuzzLevel, next);
          }}
          onFuzzToggle={() => {
            const next = !fuzzBypassed;
            setFuzzBypassed(next);
            emitParameterChange(PARAM_IDS.fuzzBypassed, next ? 1 : 0);
          }}
          onCompressorChange={(next) => {
            setCompressorAmount(next);
            emitParameterChange(PARAM_IDS.compressorAmount, next);
          }}
          onCompressorToneChange={(next) => {
            setCompressorTone(next);
            emitParameterChange(PARAM_IDS.compressorTone, next);
          }}
          onCompressorLevelChange={(next) => {
            setCompressorLevel(next);
            emitParameterChange(PARAM_IDS.compressorLevel, next);
          }}
          onCompressorToggle={() => {
            const next = !compressorBypassed;
            setCompressorBypassed(next);
            emitParameterChange(PARAM_IDS.compressorBypassed, next ? 1 : 0);
          }}
          onOctaveTransposeChange={(next) => {
            setOctaveTranspose(next);
            emitParameterChange(PARAM_IDS.octaveTranspose, next);
          }}
          onDoublerMixChange={(next) => {
            setDoublerMix(next);
            emitParameterChange(PARAM_IDS.doublerMix, next);
          }}
          onDoublerDelayChange={(next) => {
            setDoublerDelay(next);
            emitParameterChange(PARAM_IDS.doublerDelay, next);
          }}
          onDoublerDetuneChange={(next) => {
            setDoublerDetune(next);
            emitParameterChange(PARAM_IDS.doublerDetune, next);
          }}
          onOctaveToggle={() => {
            const next = !octaveBypassed;
            setOctaveBypassed(next);
            emitParameterChange(PARAM_IDS.octaveBypassed, next ? 1 : 0);
          }}
          onDoublerToggle={() => {
            const next = !doublerBypassed;
            setDoublerBypassed(next);
            emitParameterChange(PARAM_IDS.doublerBypassed, next ? 1 : 0);
          }}
          onTremoloSpeedChange={(next) => {
            setTremoloSpeed(next);
            emitParameterChange(PARAM_IDS.tremoloSpeed, next);
          }}
          onTremoloDepthChange={(next) => {
            setTremoloDepth(next);
            emitParameterChange(PARAM_IDS.tremoloDepth, next);
          }}
          onTremoloLfoChange={(next) => {
            setTremoloLfoIndex(next);
            emitParameterChange(PARAM_IDS.tremoloLfo, next);
          }}
          onTremoloToggle={() => {
            const next = !tremoloBypassed;
            setTremoloBypassed(next);
            emitParameterChange(PARAM_IDS.tremoloBypassed, next ? 1 : 0);
          }}
          onDelayMixChange={(next) => {
            setDelayMix(next);
            emitParameterChange(PARAM_IDS.delayMix, next);
          }}
          onDelayTimeLChange={(next) => {
            setDelayTimeL(next);
            emitParameterChange(PARAM_IDS.delayTimeL, next);
          }}
          onDelayTimeRChange={(next) => {
            setDelayTimeR(next);
            emitParameterChange(PARAM_IDS.delayTimeR, next);
          }}
          onDelayFeedbackChange={(next) => {
            setDelayFeedback(next);
            emitParameterChange(PARAM_IDS.delayFeedback, next);
          }}
          onDelayModeToggle={() => {
            const next = !delayModeIsDual;
            setDelayModeIsDual(next);
            emitParameterChange(PARAM_IDS.delayModeIsDual, next ? 1 : 0);
          }}
          onDelayToggle={() => {
            const next = !delayBypassed;
            setDelayBypassed(next);
            emitParameterChange(PARAM_IDS.delayBypassed, next ? 1 : 0);
          }}
          onReverbSizeChange={(next) => {
            setReverbSize(next);
            emitParameterChange(PARAM_IDS.reverbSize, next);
          }}
          onReverbDampingChange={(next) => {
            setReverbDamping(next);
            emitParameterChange(PARAM_IDS.reverbDamping, next);
          }}
          onReverbMixChange={(next) => {
            setReverbMix(next);
            emitParameterChange(PARAM_IDS.reverbMix, next);
          }}
          onReverbWidthChange={(next) => {
            setReverbWidth(next);
            emitParameterChange(PARAM_IDS.reverbWidth, next);
          }}
          onReverbToggle={() => {
            const next = !reverbBypassed;
            setReverbBypassed(next);
            emitParameterChange(PARAM_IDS.reverbBypassed, next ? 1 : 0);
          }}
        />
      ) : (
        <EqPanel
          responseCurve={responseCurve}
          leftSpectrum={leftSpectrum}
          rightSpectrum={rightSpectrum}
          analyzerEnabled={analyzerEnabled}
          eqBands={eqBands}
          eqBypassed={eqBypassed}
          onEqBandChange={(index, next) => {
            setEqBands((prev) => {
              const next2 = [...prev];
              next2[index] = next;
              return next2;
            });
            emitParameterChange(PARAM_IDS.eqBands[index], next);
          }}
          onEqBypassToggle={() => {
            const next = !eqBypassed;
            setEqBypassed(next);
            emitParameterChange(PARAM_IDS.eqBypassed, next ? 1 : 0);
          }}
          onAnalyzerToggle={() => {
            const next = !analyzerEnabled;
            setAnalyzerEnabled(next);
            emitParameterChange(PARAM_IDS.analyzerEnabled, next ? 1 : 0);
          }}
        />
      )}

      <footer className="status-bar">
        <span>Bridge active</span>
        <span>JUCE APVTS live sync</span>
      </footer>
    </main>
  );
}
