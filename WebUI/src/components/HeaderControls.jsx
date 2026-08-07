import { Knob } from "./controls";

// Header-level monitoring knobs: tempo (shared by the take recorder,
// the looper and the MIDI clock), the direct dry pass-through, and the
// Gain / Play Vol parameters. The click and MIDI clock toggles live in
// the sync strip beside the recording tabs (SyncControls.jsx), so the
// header stays a clean row of knobs.
export function HeaderControls({
  gain,
  onGainChange,
  playbackVolume,
  onPlaybackVolumeChange,
  dryLevel,
  onDryLevelChange,
  bpm,
  onBpmChange,
}) {
  return (
    <div className="header-knobs">
      <Knob
        label="BPM"
        min={40}
        max={240}
        value={bpm}
        onChange={onBpmChange}
        step="1"
        decimals={0}
        title="Tempo — shared by the take recorder, the looper and the MIDI clock"
      />
      <Knob
        label="Dry"
        min={0}
        max={1}
        value={dryLevel}
        onChange={onDryLevelChange}
        step="0.01"
        decimals={2}
      />
      <Knob
        label="Gain"
        min={0}
        max={1}
        value={gain}
        onChange={onGainChange}
        step="0.01"
        decimals={2}
      />
      <Knob
        label="Play Vol"
        min={0}
        max={1}
        value={playbackVolume}
        onChange={onPlaybackVolumeChange}
        step="0.01"
        decimals={2}
      />
    </div>
  );
}
