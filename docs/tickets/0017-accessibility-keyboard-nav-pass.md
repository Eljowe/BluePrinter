---
id: "0017"
title: "Accessibility and keyboard-navigation pass"
status: in-progress
blocked_by: []
---

# Accessibility and keyboard-navigation pass

## Problem

Most interactive controls in the WebUI are custom `div`s with pointer handlers
(knobs in `controls.jsx`, level meters, tabs, snippet cards, looper crop steppers).
Keyboard users cannot operate much of the UI and screen readers cannot interpret
it. For a release aimed at real users this is a baseline gap.

## Current behaviour

Pointer-only interaction; ad-hoc `onKeyDown` handlers exist for a few inputs
(Enter-to-blur) but there are no roles, labels, focus order, or visible focus
indicators.

## Required change

1. Audit every interactive control: knobs, toggles, tabs (Take/Loop), snippet
   cards, sort/filter chips, looper crop steppers, resize grip.
2. Add appropriate ARIA roles, accessible names, and values; `tabIndex` where
   needed; Enter/Space activation; a visible focus ring.
3. Ensure tab order follows the visual layout (masthead → sync strip → transport →
   tabs → content).
4. Respect `prefers-reduced-motion` for the splash fade and any animation.
5. Check text/UI contrast against WCAG AA where feasible.

## Acceptance criteria

- Every control is reachable and operable with the keyboard alone.
- A screen reader announces name, role, and value for knobs/toggles/meters.
- Focus is always visible; no regressions to pointer behaviour.
- Reduced-motion users get no fades.

## Docs

`AGENTS.md` (UI conventions), `webui-bridge` skill (component patterns).

## Files

`WebUI/src/components/*` (notably `controls.jsx`, `Transport.jsx`, `Looper.jsx`,
`SnippetList.jsx`, `SnippetCard.jsx`), `WebUI/src/styles.css`.

## Out of scope

High-contrast theme; full screen-reader QA across every screen reader vendor.
