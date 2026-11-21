---
id: "0064"
title: "Reduce the always-loaded AGENTS.md context budget"
status: ready-for-agent
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
