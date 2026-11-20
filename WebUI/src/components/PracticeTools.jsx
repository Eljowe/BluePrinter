import { Tuner } from "./Tuner";
import { PracticeScales } from "./PracticeScales";
import { Chords } from "./Chords";

// The Tuner / Scales / Chords buttons and their popovers, plus the Keyboard
// shortcut sheet. Rendered twice since 0059: once in the app header and once
// in the sticky quick-monitor bar. `host` says which cluster this is, and only
// the cluster named by `toolHost` renders the open popover, so the live
// analysis popover can migrate between the two as the bar appears/disappears
// without ever duplicating its DOM.
export function PracticeTools({
  host,
  className,
  activeTool,
  toolHost,
  onToggle,
  onClose,
  transport,
  playingSnippet,
  practice,
  onPersistPractice,
  chords,
  onChordsChange,
  onTunerReferenceChange,
  onTunerMonitorMuteChange,
}) {
  const owns = (tool) => activeTool === tool && toolHost === host;

  return (
    <div className={className}>
      <div className="tuner-help">
        <button
          type="button"
          data-tool="tuner"
          className={`header-tool ${activeTool === "tuner" ? "is-open" : ""}`}
          onClick={() => onToggle("tuner", host)}
          aria-expanded={activeTool === "tuner"}
          title="Built-in tuner — feed it the clean input"
        >
          Tuner
        </button>
        {owns("tuner") ? (
          <Tuner
            transport={transport}
            onReferenceChange={onTunerReferenceChange}
            onMonitorMuteChange={onTunerMonitorMuteChange}
            onClose={onClose}
          />
        ) : null}
      </div>

      <div className="scales-help">
        <button
          type="button"
          data-tool="scales"
          className={`header-tool ${activeTool === "scales" ? "is-open" : ""}`}
          onClick={() => onToggle("scales", host)}
          aria-expanded={activeTool === "scales"}
          title="Practice scales — scale tones on a standard-tuning fretboard"
        >
          Scales
        </button>
        {owns("scales") ? (
          <PracticeScales
            transport={transport}
            playingKey={playingSnippet?.key ?? ""}
            playingSourceId={transport.playingSnippetId}
            initialRoot={practice.root}
            initialScale={practice.scale}
            onPersist={onPersistPractice}
            onClose={onClose}
          />
        ) : null}
      </div>

      <div className="chords-help">
        <button
          type="button"
          data-tool="chords"
          className={`header-tool ${activeTool === "chords" ? "is-open" : ""}`}
          onClick={() => onToggle("chords", host)}
          aria-expanded={activeTool === "chords"}
          title="Chord scratchpad — pick notes and find compatible chords"
        >
          Chords
        </button>
        {owns("chords") ? (
          <Chords
            root={chords.root}
            scale={chords.scale}
            sevenths={chords.sevenths}
            selectedPcs={chords.selectedPcs}
            view={chords.view}
            previewScale={chords.previewScale}
            chordSet={chords.chordSet}
            onChange={onChordsChange}
            onClose={onClose}
          />
        ) : null}
      </div>

      <details className="shortcut-help">
        <summary title="Keyboard shortcuts">Keyboard</summary>
        <dl className="shortcut-list">
          <div><dt>Space</dt><dd>Start / stop capture (Take or Loop tab)</dd></div>
          <div><dt>Enter</dt><dd>Save the selected take's trimmed region</dd></div>
          <div><dt>Delete</dt><dd>Delete the selected take (no confirmation)</dd></div>
          <div><dt>T</dt><dd>Tap tempo</dd></div>
          <div><dt>Esc</dt><dd>Stop playback</dd></div>
          <div><dt>Ctrl/Cmd + + / −</dt><dd>Zoom the interface</dd></div>
          <div><dt>Ctrl/Cmd + 0</dt><dd>Fit the interface to the window</dd></div>
          <div><dt>F11</dt><dd>Maximise / restore the window (standalone)</dd></div>
          <div><dt>Ctrl/Cmd+Z</dt><dd>Undo overdub (active Take / Loop tab, idle)</dd></div>
          <div><dt>Ctrl/Cmd+Shift+Z / Ctrl+Y</dt><dd>Redo loop overdub</dd></div>
          <div><dt>Arrows / Page Up / Down</dt><dd>Seek focused take waveform or adjust trim handle</dd></div>
          <div><dt>Home / End</dt><dd>Move focused waveform / trim handle to its limits</dd></div>
        </dl>
      </details>
    </div>
  );
}
