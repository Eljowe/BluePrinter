---
id: "0019"
title: "Single ordered release checklist"
status: ready-for-agent
blocked_by: []
---

# Single ordered release checklist

## Problem

The release flow is spread across `installer/build-release.ps1`,
`installer/create-release.ps1`, the Inno Setup file, and `AGENTS.md`. There is no
single ordered checklist, so easy-to-miss steps — version bumps in several places,
signing order, hash verification, draft vs published — rely on memory.

## Current behaviour

Tasks and scripts exist and mostly work; the ordering and prerequisites live only
in the maintainer's head and scattered docs.

## Required change

1. Add `docs/release-checklist.md` with the ordered steps, including at minimum:
   - version bump locations (which files each need editing);
   - `npm run build` for the WebUI;
   - Release CMake build of both targets;
   - signing (0015) before assembling the bundle;
   - Inno Setup installer compile;
   - bundle assembly + `SHA256SUMS.txt`;
   - clean-machine smoke test;
   - draft GitHub release, then publish.
2. Cross-reference the exact scripts/tasks for each step so it is copy-pasteable.

## Acceptance criteria

- The checklist matches the current scripts and tasks with no stale steps.
- A release can be performed start-to-finish using only the checklist.
- Any step that needs a human (signing identity, UAC install) is called out.

## Docs

This ticket *is* the doc; link it from `README.md` and `AGENTS.md`.

## Files

New `docs/release-checklist.md`; comments in `installer/build-release.ps1` and
`installer/create-release.ps1` if the scripts need pointing back at it.

## Out of scope

Automating the checklist in CI.
