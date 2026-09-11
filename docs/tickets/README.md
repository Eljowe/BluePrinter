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
| [0016](0016-crash-diagnostics-report-flow.md) | In-app crash diagnostics report flow | ready-for-agent | — |
| [0017](0017-accessibility-keyboard-nav-pass.md) | Accessibility and keyboard-navigation pass | ready-for-agent | — |
| [0018](0018-first-run-empty-state-onboarding.md) | First-run empty states and onboarding hints | ready-for-agent | — |
| [0019](0019-release-checklist-doc.md) | Single ordered release checklist | done | — |

### Phase 2 — Hands-free control

| Ticket | Title | Status | Blocked by |
| ------ | ----- | ------ | ---------- |
| [0020](0020-global-keyboard-transport-shortcuts.md) | Global keyboard transport shortcuts | done | — |
| [0021](0021-midi-footswitch-cc-learn.md) | MIDI footswitch / CC control with learn | needs-info | — |

### Phase 3 — Capture workflow

| Ticket | Title | Status | Blocked by |
| ------ | ----- | ------ | ---------- |
| [0022](0022-fixed-length-n-bar-loop-capture.md) | Fixed-length N-bar loop capture | done | — |
| [0023](0023-take-overdub-punch-in.md) | Take recorder overdub / punch-in | needs-info | — |

### Phase 4 — Library & import

| Ticket | Title | Status | Blocked by |
| ------ | ----- | ------ | ---------- |
| [0024](0024-audio-drag-drop-import.md) | Audio drag-drop and Import into the library | ready-for-agent | — |
| [0025](0025-audio-export-formats-bounce-loop.md) | Export audio in multiple formats and bounce a loop | done | — |
| [0026](0026-midi-file-export.md) | MIDI file (.mid) export | needs-triage | — |

### Phase 5 — Decomposition

| Ticket | Title | Status | Blocked by |
| ------ | ----- | ------ | ---------- |
| [0027](0027-decompose-pluginprocessor-deep-modules.md) | Decompose PluginProcessor into deep modules | needs-triage | 0011, 0012 |

### Backlog / deferred

| Ticket | Title | Status | Blocked by |
| ------ | ----- | ------ | ---------- |
| [0028](0028-cross-platform-macos-linux.md) | Cross-platform support: macOS and Linux builds (deferred) | needs-triage | — |

## Roadmap notes

- Ordered by risk-reduction first, then user-visible wins, with the refactor
  (0027) trailing behind the tests + CI that make it safe.
- Tickets marked `needs-info` / `needs-triage` have open decisions in the body
  and are not ready for an AFK agent yet.
- **Deferred backlog**: 0028 (cross-platform) is recorded for later and must not
  be started until the Windows safety net and release path are stable and its
  open decisions are made. It would supersede ADR-0001 if green-lit.
