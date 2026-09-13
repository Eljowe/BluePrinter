# Tickets

Local issue tracker for BluePrinter. One markdown file per ticket, numbered in
creation order (`NNNN-slug.md`). This repo does **not** use GitHub Issues; tickets
are versioned alongside the code and reviewed like any other change.

The full convention (file format, statuses, operations) lives in
`docs/agents/issue-tracker.md`; the triage role strings are in
`docs/agents/triage-labels.md`.

## Convention

Each ticket starts with YAML frontmatter:

```yaml
---
id: "0001"
title: "..."
status: ready-for-agent   # ready-for-agent | ready-for-human | needs-triage | needs-info | wontfix
blocked_by: []            # ticket ids that must be closed first, e.g. ["0001"]
---
```

- **status** mirrors the triage roles in `docs/agents/triage-labels.md`.
- **blocked_by** uses local ticket ids (not `#` numbers).
- Sections: Problem, Current behaviour, Required change, Acceptance criteria,
  Docs, Files, Out of scope.

## Index

| Ticket | Title | Status | Blocked by |
| ------ | ----- | ------ | ---------- |
| [0001](0001-rework-monitor-input-output-loop-level.md) | Rework the Monitor section: Input/Output in dB, remove Dry, add Loop level | done | — |
| [0002](0002-metering-clip-latches-db-scale.md) | Add record/output metering, dB meter scale, and clip latches | done | 0001 |
| [0003](0003-monitor-only-chain-solo-mute.md) | Monitor-only chain solo/mute | done | — |
| [0004](0004-per-snippet-gain-normalize.md) | Per-snippet playback gain + normalize | done | — |
| [0005](0005-overdub-clip-guard.md) | Overdub clip guard: loop clip latch + overdub level | done | 0001 |
| [0006](0006-overdub-count-in-alignment.md) | Looper overdub count-in misaligns the layer with the loop downbeat | done | — |
| [0007](0007-cropped-loop-second-cycle-start.md) | Cropped looper: second cycle sounds like it restarts late (wrap crossfade pre-plays the loop head) | done | — |
| [0008](0008-overdub-crop-layer-offset.md) | Overdub capture with a start-cropped loop writes the layer over the loop tail | done | — |
| [0009](0009-loop-grid-quantization.md) | Looper: non-grid loop length makes the downbeat drift (second cycle starts late) | done | — |
| [0010](0010-reintroduce-global-dry-level.md) | Reintroduce a global Dry level (monitor + capture) | done | — |

### Phase 0 — Safety net

| Ticket | Title | Status | Blocked by |
| ------ | ----- | ------ | ---------- |
| [0011](0011-windows-ci-build-pipeline.md) | Windows CI build pipeline (GitHub Actions) | done | — |
| [0012](0012-ctest-harness-pure-logic-tests.md) | CTest harness + pure-logic unit tests | done | 0011 |
| [0013](0013-bridge-event-name-parity-check.md) | Automated parity check for bridge event names | done | 0012 |
| [0014](0014-context-md-and-adrs.md) | Add CONTEXT.md and ADRs for the load-bearing decisions | done | — |

### Phase 1 — Release-ready polish

| Ticket | Title | Status | Blocked by |
| ------ | ----- | ------ | ---------- |
| [0015](0015-code-signing-release-binaries.md) | Code-sign the installer and binaries | ready-for-human | — |
| [0016](0016-crash-diagnostics-report-flow.md) | In-app crash diagnostics report flow | done | — |
| [0017](0017-accessibility-keyboard-nav-pass.md) | Accessibility and keyboard-navigation pass | done | — |
| [0018](0018-first-run-empty-state-onboarding.md) | First-run empty states and onboarding hints | done | — |
| [0019](0019-release-checklist-doc.md) | Single ordered release checklist | done | — |

### Phase 2 — Hands-free control

| Ticket | Title | Status | Blocked by |
| ------ | ----- | ------ | ---------- |
| [0020](0020-global-keyboard-transport-shortcuts.md) | Global keyboard transport shortcuts | done | — |
| [0021](0021-midi-footswitch-cc-learn.md) | MIDI footswitch / CC control with learn | wontfix | — |

### Phase 3 — Capture workflow

| Ticket | Title | Status | Blocked by |
| ------ | ----- | ------ | ---------- |
| [0022](0022-fixed-length-n-bar-loop-capture.md) | Fixed-length N-bar loop capture | done | — |
| [0023](0023-take-overdub-punch-in.md) | Take recorder overdub / punch-in | done | — |

### Phase 4 — Library & import

| Ticket | Title | Status | Blocked by |
| ------ | ----- | ------ | ---------- |
| [0024](0024-audio-drag-drop-import.md) | Audio drag-drop and Import into the library | done | — |
| [0025](0025-audio-export-formats-bounce-loop.md) | Export audio in multiple formats and bounce a loop | done | — |
| [0026](0026-midi-file-export.md) | MIDI file (.mid) export | wontfix | — |

### Phase 5 — Decomposition

| Ticket | Title | Status | Blocked by |
| ------ | ----- | ------ | ---------- |
| [0027](0027-decompose-pluginprocessor-deep-modules.md) | Decompose PluginProcessor into deep modules | in-progress | 0011, 0012 |
| [0041](0041-restore-user-state-dead-code.md) | `restoreUserState` is dead code: tag names lost, DAW self-heal anchor unset | done | — |

### Phase 6 — Test depth

| Ticket | Title | Status | Blocked by |
| ------ | ----- | ------ | ---------- |
| [0029](0029-audio-path-render-modules.md) | Audio-path render modules + golden tests | done | 0012 |

### Phase 7 — Candidate roadmap (untriaged)

Brainstormed extensions. They start `needs-triage` and flip per ticket once grilled
and briefed.

| Ticket | Title | Status | Blocked by |
| ------ | ----- | ------ | ---------- |
| [0030](0030-loop-layer-undo-redo.md) | Undo/redo for looper overdub layers | done | — |
| [0031](0031-interactive-loop-waveform-crop-zoom.md) | Interactive loop waveform: drag crop handles and zoom | done | — |
| [0032](0032-time-signatures.md) | Time signatures beyond 4/4 | done | — |
| [0033](0033-named-chain-presets.md) | Named VST3 chain presets | done | — |
| [0034](0034-builtin-tuner.md) | Built-in tuner | done | — |
| [0035](0035-library-curation-setlists-favorites.md) | Library curation: setlists, favourites and bulk edits | done | — |
| [0036](0036-reverse-halfspeed-loop.md) | Reverse and half-speed loop playback | done | — |
| [0037](0037-multi-take-comping.md) | Multi-take recording and comping | done | — |
| [0038](0038-stem-per-chain-export.md) | Stem export: render each chain separately | in-progress | — |
| [0039](0039-auto-update-check.md) | Update check and in-app upgrade notice | ready-for-agent | 0015 |
| [0040](0040-plugin-manager-scan-ui.md) | Plugin manager: scan status, browse and quarantine control | done | — |

### Phase 8 — Cross-platform (macOS + Linux)

| Ticket | Title | Status | Blocked by |
| ------ | ----- | ------ | ---------- |
| [0028](0028-cross-platform-macos-linux.md) | Cross-platform support: macOS and Linux builds (spec) | in-progress | — |
| [0042](0042-portable-build-foundation-ci.md) | Portable build foundation + macOS/Linux CI | done | — |
| [0043](0043-portable-crash-diagnostics.md) | Portable crash diagnostics (macOS/Linux) | done | — |
| [0044](0044-macos-packaging.md) | macOS packaging (unsigned) | in-progress | — |
| [0045](0045-linux-packaging.md) | Linux packaging (AppImage + .deb) | in-progress | — |

## Roadmap notes

- Ordered by risk-reduction first, then user-visible wins, with the refactor
  (0027) trailing behind the tests + CI that make it safe.
- Tickets marked `needs-info` / `needs-triage` have open decisions in the body
  and are not ready for an AFK agent yet.
- **Cross-platform (0028)** was green-lit on 2026-09-13 and supersedes ADR-0001
  with ADR-0006; start with 0042 (portable build + CI), then 0043–0045.
- **Phase 7** holds untriaged candidate features spun up from the roadmap
  brainstorm; triage/grill them before claiming. 0039 sits behind 0015 so update
  delivery only ever ships signed binaries.
