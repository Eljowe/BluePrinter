---
id: "0063"
title: "Split App.jsx and styles.css into feature modules"
status: needs-triage
blocked_by: []
---

# Split App.jsx and styles.css into feature modules

## Problem

The React frontend is still global-first: `WebUI/src/App.jsx` is ~1.3k lines /
60 KB and `WebUI/src/styles.css` ~6k lines / 120 KB. Both are edited by almost
every UI change, so they are a permanent merge-conflict surface and they make
the frontend's real module boundaries invisible to a reader (and to an agent
loading context). This is the last big structural finding from the audit and the
main reason a UI change still means "read two enormous files first".

## Current behaviour

- `App.jsx` owns every backend subscription, the global state object, the
  keyboard-shortcut handler, the Take/Loop tab shell, the header layout, the
  practice-tool popovers and the library wiring.
- `styles.css` is one file with one flat namespace: feature styles are found by
  scrolling, and selectors from different features are indistinguishable.
- Components under `WebUI/src/components/` are already reasonably small (the
  largest, `PluginChain.jsx`, is ~32 KB), so the concentration is in the shell
  and the stylesheet, not the components.

## Required change

1. Extract App.jsx concerns into modules with a narrow interface, keeping
   behaviour and event names identical:
   - backend-event subscription + state reducer (e.g. `useBackendState.js`),
   - global keyboard shortcuts (`useGlobalShortcuts.js`),
   - the recording-mode/tab shell and header,
   - the library section wiring (`useLibrary.js` or a container component).
   `App.jsx` should become composition plus layout, not logic.
2. Split `styles.css` along the same feature seams (tokens/base, header,
   monitor, chains, recording, looper, library, practice tools), imported from
   one entry point so the build output stays a single stylesheet. Keep the
   existing class names — this is a move, not a rename pass — so the visual
   result is byte-for-byte stable.
3. No behaviour change: same `bridge.js` API, same events, same layout, same
   accessibility roles.

## Acceptance criteria

- `App.jsx` is under ~300 lines and contains no backend event wiring or state
  reducer logic.
- No CSS file exceeds ~1500 lines; the built `WebUI/dist` CSS hash changes only
  because of ordering, and a visual check (headless screenshot or a real
  WebView2 pass) shows no layout difference.
- `BridgeEventParity`, `ScaleTheory` and `ChordTheory` stay green; `npm run
  build` is green.
- AGENTS.md/`webui-bridge` skill file maps updated to name the new modules.

## Files

`WebUI/src/App.jsx`, `WebUI/src/styles.css`, new hooks/module files under
`WebUI/src/`, `AGENTS.md`, `.opencode/skills/webui-bridge/SKILL.md`.

## Out of scope

- Renaming CSS classes or redesigning any screen.
- TypeScript migration, a state library, or a component-folder reorganisation.

## Comments

2026-11-21 — Filed from the audit's structural findings. Not started: it is a
pure refactor with no user-visible payoff, so it should be done in one quiet
pass with a real-webview visual check.
