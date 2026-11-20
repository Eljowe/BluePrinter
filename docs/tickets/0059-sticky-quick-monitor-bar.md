---
id: "0059"
title: "Sticky quick-monitor bar: keep the monitor knobs and practice tools reachable while scrolled"
status: in-progress
blocked_by: []
---

# Sticky quick-monitor bar: keep the monitor knobs and practice tools reachable while scrolled

## Problem

The Monitor deck (BPM/Tap, Input/IN, REC, Output/OUT, Dry) and the header
Tuner / Scales / Chords / Keyboard buttons all live at the top of the page. In a
tall session — chains, takes, the library — checking or trimming the input while
auditioning a snippet near the bottom means scrolling all the way back up and
losing your place. The fixed 960×700 design gives very little vertical room, so
the answer cannot be a second full-size deck.

## Current behaviour

`.app` is one scrolling column: `.app-header` (wordmark + tools), then the
numbered sections (Monitor is `01`). Scrolling happens on `.ui-scroll`, and
`.ui-content` is scaled with `transform: translateX(offsetX) scale(scale)` by
`UiScale.jsx`, so `position: fixed` inside the content scales and scrolls with
it. The tool popovers are `position: absolute` children of their header button
(`.tuner-help` / `.scales-help` / `.chords-help`). Input/Output/Dry state and
all meter values already reach the frontend through `transport`; the tool
handlers (`handleToolToggle`, `handleToolClose`) already exist in `App.jsx`.

## Design decision (agreed)

A single **condensed overlay bar** that slides down once the Monitor section has
scrolled past, and stays visible for the rest of the scroll. It carries only
what is needed at a glance:

- Left: the `01` section index badge, then `Tuner`, `Scales`, `Chords`,
  `Keyboard` as the same `.header-tool` buttons.
- Right, right-aligned: `Input + IN meter`, `REC` (meter only), `Output + OUT
  meter`, `Dry` (no meter). Knobs are 32px with the dB value inline; each meter
  is a 2px horizontal track with a peak tick and a red clip dot that resets
  that bus's latch on click.

Explicitly **not** in the bar: BPM/Tap tempo and record/transport state (the
user declined both; the Take/Loop tabs and transport stay in their sections).

Behaviour:

- Appears once the Monitor section has scrolled fully above the viewport
  (`section.bottom <= 0`) and hides once its top is back at/below the top edge
  (`section.top >= 0`). The two different edges give a dead band as tall as
  the section, so the boundary cannot flicker. Never shown at the top of the
  page and never during the splash (the splash locks scrolling anyway).
- Always visible once past Monitor — no scroll-direction hiding.
- Slides in with `translateY(-100%) → 0` over ~180ms ease-out (exit ~120ms).
  The existing `prefers-reduced-motion` rule already flattens the animation.
- It is an overlay: no layout shift, no reserved flow height.
- Bar knobs are **drag + keyboard only** (`disableWheel`); a permanently pinned
  knob row must not turn page scrolling into dB edits.
- The open tool popover **migrates** between the header cluster and the bar
  cluster as the threshold is crossed, so an open Tuner/Scales/Chords neither
  scrolls away nor is stranded when scrolling back up. One popover instance at
  a time, hosted by the currently visible trigger.
- `scroll-padding-top` on `.ui-scroll` is raised while the bar is shown so
  focused controls never land underneath it.

## Required change

1. **Scaling/overlay plumbing** — `UiScale.jsx` exposes `{ scale, offsetX,
   overlayNode }` through a context and renders an overlay host inside
   `.ui-viewport` (sibling of `.ui-scroll`) whose inner element gets the same
   `translateX(offsetX) scale(scale)` transform with `transform-origin: top
   left`. The host is `pointer-events: none`; the bar re-enables pointer events.
   This keeps the bar outside the scrolling transformed content (robust in
   WebView2) while still scaling with the UI.
2. **`QuickBar.jsx`** (new) — renders the bar into `overlayNode` via
   `createPortal`, reusing `Knob`, the shared `LevelMeter` (compact variant),
   and the extracted `PracticeTools` cluster (Tuner/Scales/Chords + Keyboard).
   Visibility is driven by an rAF-throttled scroll/resize listener on
   `.ui-scroll` reading the Monitor section's `getBoundingClientRect()`.
   Wheel events over the bar (which lives outside the scroll container) are
   forwarded to `.ui-scroll`, except over a popover's own scroll area.
3. **`App.jsx`** — compute `barVisible` (the shared trigger), render
   `<QuickBar>`, and refactor the tool wiring so both clusters can drive it:
   - `toolHost` state (`"header" | "bar"`) set on toggle and kept in sync with
     `barVisible`.
   - outside-pointer dismissal uses `closest()` on the tool containers/popovers
     instead of the three trigger refs (which can only point at one cluster);
   - focus-return stores the actual trigger element at open time.
4. **`controls.jsx`** — add an opt-in `disableWheel` prop to `Knob` (no change
   to default behaviour).
5. **`styles.css`** — `.ui-overlay` / overlay inner, `.quick-bar` (56px,
   paper background, 3px bottom rule, 30px inner padding, flex row), compact
   knob/meter styling, and the enter/exit keyframes.

No C++, `WebViewEditor`, `bridge.js` or event changes: every value used by the
bar is already in the 30Hz `backendTransport` snapshot.

## Acceptance criteria

- Scrolling past the Monitor section slides a 56px bar down from the top within
  one frame of the deck leaving; it stays visible until scrolling back above the
  deck, then slides out.
- The bar shows Input/IN, REC, Output/OUT and Dry, and their knobs change the
  same values as the deck (both stay in sync because they read the same
  `transport`/handlers).
- Clicking a clip dot clears that bus's latch; the meter level and peak track
  live audio at 30Hz.
- Scrolling with the pointer over a bar knob scrolls the page; dragging or
  arrow keys changes the value.
- Tuner/Scales/Chords open from either cluster, close on outside click /
  Escape, and remain operable when the threshold is crossed while open.
- No permanent layout shift, no duplicate popover DOM, and no console errors.
- Keyboard tabbing after the bar appears never focuses a control hidden under
  it (`scroll-padding-top`).
- `prefers-reduced-motion` removes the slide.
- Must be verified in a real WebView2 window, not only headless Chrome.

## Docs

`AGENTS.md` (UI scaling / startup splash section), webui-bridge skill
(component + accessibility conventions).

## Files

`WebUI/src/components/UiScale.jsx`, `WebUI/src/components/QuickBar.jsx` (new),
`WebUI/src/App.jsx`, `WebUI/src/components/controls.jsx`,
`WebUI/src/styles.css`, `WebUI/src/components/HeaderControls.jsx` (only if the
compact monitor cells are shared).

## Out of scope

- BPM/Tap tempo and record/transport controls in the bar (declined).
- Auto-hiding on scroll-down / revealing on scroll-up.
- Making the existing header or Monitor deck sticky-and-shrinking.
- Any new backend event or persisted preference.

## Comments

### 2026-11-03 — implemented, pending real-WebView2 smoke test

Implemented as `QuickBar.jsx` + the `PracticeTools` extraction, the
`UiScale` overlay layer/context, `Knob`'s `disableWheel` opt-out, and the
visibility/focus/wheel wiring in `App.jsx` + `styles.css`. No bridge or
C++ changes.

Verified with headless Chrome (944×605, scale ≈ 0.86) over CDP:

- Bar hidden at the top; slides in once the Monitor section is fully above
  the viewport. The dead band (hide only when the section top returns) holds
  at scrollTop 280/200/150 and hides at 100/0; no flicker.
- Bar is 56 design px (48.4 screen px), aligns with the 30px app padding,
  reports no horizontal overflow, and its meters/knobs read the same
  transport state as the deck (bar Input arrow key updates the deck Input
  `aria-valuetext` from `0.0 dB` to `0.5 dB`).
- Popovers hosted in the bar are single-instance, land at `left: 30px`
  under the bar (Chords 780px, Scales 380px), close on outside pointerdown,
  and migrate from the header to the bar when the threshold is crossed while
  Tuner is open.
- Wheel over the bar forwards to `.ui-scroll`; wheel over an open popover
  scrolls the popover instead.
- No runtime exceptions.

Still to check in a real WebView2 window (font metrics differ from headless
Chrome, and `transform`/`visibility` interactions are the point of the
overlay): bar text/knob widths and the exit/enter animation.
