# Architecture docs

On-demand implementation detail moved out of [`AGENTS.md`](../../AGENTS.md) (ticket 0064).
`AGENTS.md` keeps the rules, the map and a summary per topic; these files keep the mechanics and the
incident history verbatim. Read them when working in that area.

- [`audio-path.md`](audio-path.md) — signal flow, chain model, routing/MIDI limits, PDC,
  recording/capture, takes and loops, stems, meters/guard, tuner and melody, MIDI
  clock/click/rehearsal, bus layout.
- [`chain-restore.md`](chain-restore.md) — chain serialisation and migration, parameter-change
  persistence, the deferred one-plugin-per-turn restore, crash marker/self-heal, crash diagnostics,
  plugin quarantine, presets, plugin manager, editor windows.
- [`webui.md`](webui.md) — recording tabs, shortcuts, header/sync controls, bridge events and
  payloads, library/setlists, Scales/Chords, accessibility, resize/maximise, UI scaling, startup
  splash, song builder and rehearsal.
- [`build-and-ci.md`](build-and-ci.md) — build system (JUCE patch, ASIO, output formats), VS Code
  tasks, release scripts, CI jobs, lint/format.

See also [`CONTEXT.md`](../../CONTEXT.md) (language, module map, invariants) and
[`docs/adr/`](../adr/) (load-bearing decisions).
