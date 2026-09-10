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
