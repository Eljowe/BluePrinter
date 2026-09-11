---
id: "0013"
title: "Automated parity check for bridge event names"
status: done
blocked_by: ["0012"]
---

# Automated parity check for bridge event names

## Problem

Every frontend↔backend event name is duplicated: a `static constexpr const char*`
in `Source/WebViewEditor.h` (`frontend*Event` / `backend*Event`) and a string map
in `WebUI/src/bridge.js` (`FRONTEND_EVENTS` / `BACKEND_EVENTS`). Nothing links the
two, so a rename on one side alone compiles and builds cleanly but fails silently
at runtime. AGENTS.md warns about exactly this class of bug ("never hardcode event
name strings").

## Current behaviour

Parity is enforced by developer discipline only.

## Required change

1. Add a check that parses the `WebViewEditor.h` constants and the `bridge.js`
   event maps and asserts the two sets match exactly.
2. Implement it as part of the 0012 test harness, or as a small standalone script
   if that is simpler; either way it must run in CI.
3. On mismatch, print the diff (names present on one side only).

## Acceptance criteria

- The check passes against the current code.
- Renaming an event on one side only makes the check fail with a readable diff.
- The check runs in CI.

## Docs

`webui-bridge` skill — note that event names are now machine-checked.

## Files

New test/script (near `Tests/`), `.github/workflows/build.yml`,
`WebUI/src/bridge.js` (only if a stable export shape is needed),
`Source/WebViewEditor.h`.

## Out of scope

Validating event payload shapes/field names.
