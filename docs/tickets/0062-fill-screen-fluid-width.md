---
id: "0062"
title: "Fill the screen: maximise to the whole work area and let the UI use the width"
status: done
blocked_by: []
---

# Fill the screen: maximise to the whole work area and let the UI use the width

## Problem

0060's Maximise keeps the 960:700 aspect ratio, so on a 16:9 display it fills
only the height and leaves a large strip of desktop unused on both sides. Even
inside the resulting window the Fit scale is width/height-min limited, so the
content can letterbox a few dozen pixels. The user wants the window to use the
whole screen and the interface to actually take advantage of the extra width —
the FX chain rack and the library grid are the obvious beneficiaries.

## Current behaviour

- `WebViewEditor::setWindowMaximised(true)` asks `EditorWindowMath` for the
  largest **aspect-correct** content size, and the editor's constrainer keeps
  `setFixedAspectRatio(designAspect)` with 0.7×–2× limits, so the standalone
  window can never be wider than 960×scale.
- `UiScale.jsx` always lays the content out at exactly 960 CSS px
  (`.ui-content { width: 960px }`) and only scales it; `offsetX` centres the
  remainder. `.app` also has `max-width: 1200px`.
- Result: on a 1920×1080 work area the window is ~1426 px wide, the content is
  centred with side gutters, and the chain rail/library cannot grow.

## Required change

1. **Window fills the work area.**
   - Replace the aspect-fit helper with a pure `computeFillContentSize(workW,
     workH, chromeW, chromeH, minW, minH)` (window chrome subtracted, minimum
     editor size respected). It can be any aspect.
   - While maximised, relax the editor's constrainer: `setFixedAspectRatio(0)`
     and a large `setSizeLimits` (the editor's own fixed limits would otherwise
     clamp the standalone window through `DecoratorConstrainer`). On restore,
     re-apply the 0.7×–2× limits and `designAspect`.
   - A grip resize while maximised re-applies the aspect lock first (as now, it
     leaves maximise).
   - Keep the existing persistence rules: the pre-maximise size/position are
     restored, the maximised bounds are never persisted, and quitting while
     maximised saves the pre-maximise size.
2. **Fluid width in Fit.**
   - In `UiScale`, when `zoom === "fit"`, keep `scale = min(w/960, h/700)` but
     set the content width to `max(960, viewportWidth / scale)` so the surface
     fills the viewport exactly (no side gutters). `offsetX` stays 0 in Fit.
   - Manual zoom stays absolute: content width 960, centred/scrollable as
     before, so the percentage still means the 960 design size.
   - The overlay (`useUiScale`) follows the same width/transform.
   - Remove `.app { max-width: 1200px }` so the stretched width reaches the
     sections. Spread the Monitor knob row across the wider deck.
3. **Docs/tests.** Update the `EditorWindowMath` CTest for the fill helper;
   update AGENTS.md + webui-bridge skill + ticket 0060's later comment.

## Acceptance criteria

- Maximising fills the display work area (title bar retained) on a 16:9 screen;
  Restore returns to the previous size/position.
- In Fit, the UI fills the window both ways at any aspect: no paper gutters
  inside the window, the chain rack shows more cards and the library grid gains
  columns on a wide screen.
- Manual zoom keeps the absolute 960-wide surface (centred below Fit, two-axis
  scroll above it).
- A grip resize leaves maximise and returns to the aspect-locked behaviour.
- Quitting while maximised relaunches at the last normal size.
- Bridge parity, WebUI build and the math CTest are green.

## Files

`Source/WebViewEditor.h/.cpp`, `Source/EditorWindowMath.h`,
`Tests/test_EditorWindowMath.cpp`, `WebUI/src/components/UiScale.jsx`,
`WebUI/src/styles.css`, docs.

## Out of scope

- Borderless/fullscreen-exclusive mode (title bar stays, as in 0060).
- A multi-column dashboard redesign; the existing vertical sections simply
  grow.

## Comments

2026-11-03 — Implemented. Maximise now fills the work area (any aspect) and
relaxes/re-applies the editor constrainer around it; `EditorWindowMath` became
`computeFillContentSize` with updated CTest; `UiScale` Fit is fluid
(`max(960, viewportWidth / scale)`) while manual zoom stays absolute; `.app`
lost its 1200px cap and the Monitor knob row spreads across the deck. The quick
bar overlay and its pre-scaled transform follow the fluid width.

Verified: WebUI `vite build` green; bridge parity green (111 events); math
helper compiled and exercised with g++; headless Chrome at 1584×805 reports a
1574px Fit surface with no horizontal overflow (manual 125% → 1200px centred),
overlay present, no renderer exceptions. Full C++ build still needs a
Windows/CI pass (no local MSVC instance).
