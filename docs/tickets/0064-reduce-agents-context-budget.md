---
id: "0064"
title: "Reduce the always-loaded AGENTS.md context budget"
status: done
blocked_by: []
---

# Reduce the always-loaded AGENTS.md context budget

## Problem

`AGENTS.md` is ~68 KB (92 very long lines) and is loaded into every session as
project instructions. Most of it is historical incident detail — the crash
forensics, quarantine war stories, restore-driver archaeology and per-feature
changelogs that were written while fixing them. That detail is genuinely
valuable, but carrying all of it in the always-on prompt crowds out the code an
agent is supposed to read, and it drifts (this pass fixed several stale claims:
"Windows-only", the retirement model, the MIDI send path, the version).

The last audit flagged the 65 KB always-loaded budget as a structural finding:
the fix is to move archaeology out of the always-on file, not to delete it.

## Current behaviour

- `AGENTS.md` mixes three kinds of content: (a) rules and conventions an agent
  must follow, (b) a map of where things live, (c) long "why it was broken and
  how it was fixed" narratives.
- The narratives duplicate `CONTEXT.md` (language/invariants) and `docs/adr/`
  (load-bearing decisions), and some are already summarised there.
- There is no per-area doc for the heaviest (c), so there is nowhere else to put
  it when the always-on file is trimmed.

## Required change

1. Classify each `AGENTS.md` section as rule / map / narrative.
2. Move the narrative detail into focused docs that load on demand (e.g.
   `docs/architecture/chain-restore.md` for the deferred restore + self-heal
   history, `docs/architecture/audio-path.md`, `docs/architecture/webui.md`),
   leaving in `AGENTS.md` a 1–3 sentence summary plus a link per topic.
3. Keep `AGENTS.md` as the single always-on file: it must still state the
   invariants, build/test commands, event-naming rule, chain ownership model,
   restore rules and the pointer list. Target well under ~30 KB without losing
   a single fact.
4. Update `CONTEXT.md`'s module map and the skills (`blueprinter`,
   `webui-bridge`, `juce-audio`) to point at the new docs; the specialist skills
   are the right place for per-area mechanics.
5. No code changes. Every claim that stays must be true against the code at the
   time of the move.

## Acceptance criteria

- `AGENTS.md` is ≤ ~30 KB and contains no multi-paragraph incident narrative;
  every moved topic is reachable from it in one link.
- Every fact moved is still present somewhere in `docs/`; nothing is silently
  dropped (checklist in the PR description: section → destination).
- The skills' "where to read next" lists name the new docs.
- `docs/agents/domain.md` still describes the doc system accurately.

## Files

`AGENTS.md`, new files under `docs/architecture/`, `CONTEXT.md`,
`docs/agents/domain.md`, `.opencode/skills/*/SKILL.md`.

## Out of scope

- Rewriting `CONTEXT.md` or the ADRs (they are already the right size and shape).
- Any code, build or CI change.
- Changing agent behaviour rules — the rules stay in `AGENTS.md` verbatim.

## Comments

2026-11-21 — Filed from the audit's structural findings and marked
`ready-for-agent`: the work is mechanical (classify, move, link, verify) and
needs no design decisions, only care not to lose a fact. Do it in a dedicated
pass and land it alone so the diff is auditable.

2026-11-21 — Moved (branch `feature/0064-agents-context-budget`, left uncommitted
for review). Section → destination:

- **Project Overview** → `AGENTS.md` (unchanged).
- **Build System**, **VS Code tasks**, release scripts, **CI**, lint notes →
  [`docs/architecture/build-and-ci.md`](../architecture/build-and-ci.md);
  `AGENTS.md` keeps the JUCE/patch line, the CMake pointer and the WebUI build
  line.
- **Commands** → `AGENTS.md` (build/test commands); CI/lint detail in
  `build-and-ci.md`.
- **Coding Conventions** → the C++/threading/JS/APVTS rules stay verbatim in
  `AGENTS.md`; tuner analysis and melody → `audio-path.md`; Scales/Chords →
  `webui.md`; metering/Guard → `audio-path.md`; snippet gain/seeking/favourite/
  export, import, library toolbar, setlists and songs → `webui.md`; stems,
  load-into-looper and loop level/reverse → `audio-path.md`.
- **Event Naming** → `AGENTS.md` (unchanged).
- **VST3 Chains** → ownership/retirement, routing, the PDC headline, the
  recording-mix definition and the restore/persist rules stay verbatim in
  `AGENTS.md`; chain fields/defaults/CPU readout, the MIDI merge limitation and
  the PDC/capture mechanics → `audio-path.md`; serialisation, parameter
  listeners, deferred restore, crash marker/self-heal, crash diagnostics,
  quarantine, presets, plugin manager and editor windows → `chain-restore.md`.
- **Tabs, shortcuts, header/sync strip, bridge events/payloads, chain rail,
  accessibility, resize/maximise, UI scaling, splash** → `webui.md`.
- **Song builder and rehearsal** → `webui.md`.
- **Skills / Agent skills** → `AGENTS.md`; the Domain-docs closing sentence now
  points at `docs/architecture/` instead of claiming detail stays in
  `AGENTS.md`.

`CONTEXT.md`, `docs/agents/domain.md` and the `blueprinter` / `webui-bridge` /
`juce-audio` skills now point at the new docs (and the new
`docs/architecture/README.md` index).

**Verification.** Old `AGENTS.md` split into 330 sentence/line units and
whitespace-normalized, then checked against `AGENTS.md` + `docs/architecture/*.md`:
**3 misses**, all explained. Two are the halves of the rewritten Domain-docs
closing sentence (“…deep implementation detail stays in this file.” now reads
“…lives in `docs/architecture/`”): that sentence asserted the fact this ticket
changes, and the ADR list plus the rest of the paragraph are verbatim. The third
differs only in a link target: "follow
`docs/release-checklist.md`" in `build-and-ci.md`
now points at `../release-checklist.md` so it resolves from `docs/architecture/`
— the sentence text is otherwise identical. Every other unit matches verbatim. A
120-char slice probe (1,175 probes) reports 92 misses, all of which decompose
into sentence fragments that each match — they only span the inserted
bullet/section/document boundaries.

`AGENTS.md` is **14,690 bytes** (from 79,522). New docs: `audio-path.md`
22,369, `chain-restore.md` 18,049, `webui.md` 28,160, `build-and-ci.md` 5,209,
`README.md` 1,257. UTF-8 without BOM, no trailing whitespace, no code changes.

- 2026-11-21: Reviewed and closed. AGENTS.md is 14,690 bytes;
  the 330-unit coverage check has 3 explained misses (the Domain-docs pointer
  sentence changed by this ticket and the release-checklist link retargeted for
  the new directory); all other units are verbatim. No code changes.
