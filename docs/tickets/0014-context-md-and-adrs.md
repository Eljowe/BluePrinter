---
id: "0014"
title: "Add CONTEXT.md and ADRs for the load-bearing decisions"
status: done
blocked_by: []
---

# Add CONTEXT.md and ADRs for the load-bearing decisions

## Problem

The domain-docs convention (`docs/agents/domain.md`) expects a single-context
`CONTEXT.md` plus `docs/adr/`, and neither exists. The design rationale lives only
in a 37 KB `AGENTS.md` and in commit history, which is hard to navigate and
inflates every agent's context with implementation detail instead of language.

## Current behaviour

No `CONTEXT.md`; no `docs/adr/`. All model detail is in `AGENTS.md`.

## Required change

1. Create a root `CONTEXT.md` (single-context) following
   `docs/agents/domain.md`: ubiquitous language (take, loop, chain, capture mix,
   dry, count-in, pending take, quarantine), a module map, and the key invariants.
   Link to `AGENTS.md` for deep implementation detail rather than duplicating it.
2. Create `docs/adr/` with ADRs (context / decision / consequences) for at least:
   - Windows-only, WebView2-based UI;
   - deferred one-plugin-per-turn chain restore, with quarantine + self-heal;
   - chains run in parallel and never hear each other's output;
   - persist/flush gated while a chain restore is in progress;
   - audio-only looper (MIDI event sequencing deliberately removed).

## Acceptance criteria

- `CONTEXT.md` exists, is accurate to current behaviour, and does not duplicate
  `AGENTS.md` content that will rot.
- At least four ADRs exist in `docs/adr/`, each with context, decision, and
  consequences.
- Format matches `docs/agents/domain.md` and its `CONTEXT-FORMAT` / `ADR-FORMAT`
  references.

## Docs

This ticket *is* the docs; update `AGENTS.md` to point at `CONTEXT.md`.

## Files

New `CONTEXT.md`, new `docs/adr/*.md`.

## Out of scope

Wholesale migration of `AGENTS.md` content or rewriting `AGENTS.md`.
