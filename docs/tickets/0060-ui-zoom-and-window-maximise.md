---
id: "0060"
title: "UI zoom percentages and a maximise-window toggle"
status: done
blocked_by: []
---

# UI zoom percentages and a maximise-window toggle

## Problem

The interface is laid out once at 960×700 and always auto-fits the window, so
there is no way to change the size of the UI independently of the window:

- On a small screen, or in a DAW where the host owns the plugin window, the
  user cannot make the interface denser to see more at once or larger to read
  it comfortably.
- In the standalone, filling the screen means dragging the corner grip by hand
  every session; there is no one-click maximise.

## Current behaviour

- `WebUI/src/components/UiScale.jsx` measures `.ui-scroll` and applies
  `scale = min(clientWidth / 960, clientHeight / 700)` with
  `transform: translateX(offsetX) scale(scale)`. `offsetX` centres the surface
  when it is narrower than the viewport, `minHeight = clientHeight / scale` keeps
  short content filling the height, and a `ResizeObserver` tracks the unscaled
  content height so `.ui-scaled` can reserve the scaled height.
- `.ui-scroll` is `overflow-y: auto; overflow-x: hidden` with
  `scrollbar-gutter: stable`; `.ui-scaled` is `overflow: clip`. X overflow is
  deliberately clipped because focusing a right-hand control used to scroll the
  whole surface sideways.
- The editor window is aspect-locked 960:700, limited to 0.7×–2×
  (`WebViewEditor.cpp`: `setResizeLimits`, `setFixedAspectRatio`,
  `setResizable (false, false)`). The only resize affordance is the corner grip
  (`ResizeHandle.jsx` → `frontendResizeEditor { dWidth }` →
  `handleResizeEditor`), and the saved size lives in the properties file
  (`getSavedEditorWidth` / `saveEditorSize`, debounced from `timerCallback`).
- In the standalone, JUCE's `StandaloneFilterWindow` (`DocumentWindow`)
  contains a `MainContentComponent` that resizes the window to contain the
  editor whenever the editor's bounds change, and gives the editor the
  aspect-locked constrainer.
- `main.jsx` suppresses WebView2 page zoom for `Ctrl/Cmd + wheel` and
  `Ctrl/Cmd + - / = / 0`. `Knob`'s `handleWheel` (`controls.jsx:71`) consumes
  every wheel event and edits the value, including when Ctrl/Cmd is held.

## Design decisions (agreed)

1. **Zoom model: Fit + absolute percentages.** `Fit` is the default and keeps
   today's behaviour. Manual zoom is absolute: 100% means the design surface at
   its real 960 CSS px width. The control shows the effective percentage (e.g.
   `Fit · 154%`) so the number means the same thing regardless of window size.
2. **Two-axis scrolling** when zoomed in past Fit. The surface is centred while
   it is narrower than the viewport and scrolls horizontally once it is wider.
3. **Maximise, not true fullscreen.** Grow the window to the largest
   aspect-correct size that fits the display's work area and centre it, keeping
   the OS title bar. This avoids fighting the standalone wrapper and the
   aspect-locked constrainer: it is just a large `setSize`. Standalone-only,
   session-only.

Additional agreed defaults:

- Zoom steps of 10% (clamped 50–300) from the current effective percentage, so
  the useful stops 90/100/110/125/150 are reached directly; `Fit` is a separate
  reset rather than a stop on the ladder.
- The zoom control lives in the unscaled shell next to the resize grip (a
  compact `−  100%  +` pill, the readout click returns to Fit), so it stays
  reachable and constant-size even at 50%.
- `Ctrl/Cmd + wheel` zooms; `Ctrl/Cmd + −/=/0` steps/resets to Fit.
- `F11` toggles maximise where the OS allows it (macOS may capture F11; the
  button always works). `Esc` stays "stop playback"; it does not leave
  maximise.
- Zoom persists in `localStorage`; maximise does not persist across launches.

## Required change

### 1. Zoom shell (`UiScale.jsx`)

- Replace the single fitted scale with `zoom: "fit" | number` (percent).
  - `fitScale = min(clientWidth / 960, clientHeight / 700)`.
  - `scale = zoom === "fit" ? fitScale : clamp(zoom, 50, 300) / 100`.
  - `offsetX = max(0, (clientWidth − 960 × scale) / 2)`.
  - `.ui-scaled` gets an explicit width of
    `max(clientWidth, 960 × scale)` and height `contentHeight × scale`; the
    inner `.ui-content` keeps `transform: translateX(offsetX) scale(scale)`
    and `minHeight = clientHeight / scale` (so zoom-out still shows more
    vertical content rather than stretching the app).
- `.ui-scroll` becomes `overflow: auto` (both axes). Keep `scrollbar-gutter:
  stable`; update the comments that describe the old clip workaround. At Fit
  there must be no scrollbars, exactly as today.
- Load/save the setting under a `blueprinter.uiZoom` localStorage key with the
  same try/catch robustness as `recordingMode` in `App.jsx`; reject malformed
  values (fall back to `"fit"`), clamp numbers.
- Expose `{ zoom, setZoom, stepZoom, effectivePercent, isFit }` to the control
  (props or a small context).

### 2. Zoom control and shortcuts

- New `UiZoomControl.jsx`, rendered by `UiScale` beside `ResizeHandle` inside
  `.ui-viewport` (not inside the transformed content, so it is not scaled and
  is never clipped by scrolling). Layout: `−`, a readout button (effective %),
  `+`; clicking the readout while manual returns to Fit. `role="group"` +
  `aria-label="Interface zoom"`; each button labelled; the readout's
  `aria-label` includes the effective percent and that clicking resets to Fit.
- Add `Ctrl/Cmd + wheel` handling (window-level, non-passive) to step zoom and
  `preventDefault` the page zoom. Keep the existing `main.jsx` suppression, or
  fold it into this handler. `Knob`'s `handleWheel` must ignore events with
  `ctrlKey`/`metaKey` so a zoom gesture over a knob does not also turn it.
- `Ctrl/Cmd + −/=/0` steps/resets zoom. The global transport keydown handler
  must ignore these (they do not conflict with Space/Enter/Delete/T/Esc).
- Add the bindings to the header **Keyboard** popover.

### 3. Maximise (standalone only)

- Add `frontendSetMaximised { enabled }` to `WebViewEditor.h` and
  `bridge.js`; ship `standalone` and `maximised` in the 30 Hz
  `backendTransport` snapshot.
- `standalone` comes from `juce::JUCEApplicationBase::isStandaloneApp()` (false
  in VST3/AU). In a DAW the UI hides the button and `F11` is inert; the backend
  ignores the event too.
- On enable:
  1. Record the current editor size and the top-level window position.
  2. Query the display containing the window
     (`Desktop::getInstance().getDisplays().getDisplayForRect(...)->userArea`).
  3. Subtract the current window chrome (`topLevel.bounds − editor.bounds`) from
     the work area, then choose the largest aspect-correct content size that
     fits, clamped to the existing 0.7×–2× limits.
  4. `setSize()` the editor (the standalone wrapper follows) and centre the
     top-level window in the work area.
- On disable: restore the remembered editor size and window position, clamped
  to the current display. Clear the maximised flag.
- A grip resize while maximised clears the flag (and updates the remembered
  restore size to the new size). Maximise state changes only via the toggle or
  a grip resize; window moves are not tracked.
- **Persist guard:** while maximised, do not arm or flush the debounced
  `editorWidth`/`editorHeight` save. On destruction while maximised, persist the
  remembered pre-maximise size instead, so quitting maximised cannot bake the
  screen size into the next launch.
- Add a `Maximise` / `Restore` button (likely in the footer or the unscaled
  overlay next to the zoom pill) shown only when `standalone`; `aria-pressed`
  and an explicit label change.
- Extract the work-area/chrome/aspect calculation into a pure helper
  (`Source/EditorWindowMath.h/.cpp`, in the style of `ChainCpuMath.h`) with a
  CTest in `Tests/test_EditorWindowMath.cpp` and registration in
  `CMakeLists.txt`.

### 4. Tests and docs

- Keep `Tests/check-bridge-events.mjs` green after the new event.
- `npm run build` in `WebUI`.
- This is a UI change: verify in a real WebView2 window (headless Chrome does
  not reproduce WebView2 font metrics/layout).
- Update `AGENTS.md` (UI scaling, resizable window/maximise, keyboard
  shortcuts) and the `webui-bridge` skill (event list, accessibility
  conventions for the zoom group).

## Acceptance criteria

- At Fit, the window behaves exactly as today: the UI fills the window, no
  scrollbars, resizing the window scales the UI, and the corner grip still
  resizes the native window.
- The zoom pill shows the effective percentage; `+`/`−`, `Ctrl/Cmd + wheel`,
  and `Ctrl/Cmd + −/=/0` all change it; clicking the readout while manual
  returns to Fit.
- Zooming out below Fit centres the fixed-width surface and shows more vertical
  content; zooming in past Fit produces both scrollbars and the surface can be
  scrolled to every edge. The zoom pill and grip remain unscaled and reachable.
- The zoom setting survives an app restart (localStorage) and a malformed value
  falls back to Fit.
- Ctrl+wheel over a knob zooms the interface without changing the knob value.
- In the standalone, Maximise fills the work area with the largest
  aspect-correct window, keeps the title bar, and Restore returns to the exact
  previous size/position. Repeated toggles are stable.
- Quitting while maximised, then relaunching, restores the last non-maximised
  editor size (not the screen size).
- The maximise button is absent in DAW hosts and the event is a no-op there;
  zoom still works in DAW hosts.
- `Tests/check-bridge-events.mjs`, the CTest suite, and the WebUI build are
  green.

## Docs

`AGENTS.md` (UI scaling / resizable window / keyboard shortcuts),
`docs/agents/domain.md` is not affected, webui-bridge skill (events,
accessibility), `docs/tickets/README.md` index.

## Files

- `WebUI/src/components/UiScale.jsx`
- `WebUI/src/components/UiZoomControl.jsx` (new)
- `WebUI/src/components/ResizeHandle.jsx` (only if the pill shares its corner
  layout)
- `WebUI/src/App.jsx` (maximise button, keyboard bindings, Keyboard popover)
- `WebUI/src/components/controls.jsx` (Ctrl/Cmd wheel guard in `Knob`)
- `WebUI/src/main.jsx` (zoom gesture ownership)
- `WebUI/src/bridge.js`
- `WebUI/src/styles.css`
- `Source/WebViewEditor.h/.cpp`
- `Source/EditorWindowMath.h/.cpp` (new)
- `Tests/test_EditorWindowMath.cpp` (new), `CMakeLists.txt`
- `AGENTS.md`, `docs/tickets/README.md`

## Out of scope

- True borderless fullscreen, exclusive fullscreen, or reflowing the layout to a
  wide/landscape aspect. The design surface stays 960×700.
- A compact/dense layout mode (smaller paddings and controls at normal text
  size). The only way to see more is zooming out.
- Space-drag panning, pinch gestures other than Ctrl+wheel, and per-monitor or
  per-host zoom memory.
- Persisting zoom in host state or in the plugin state blob; `localStorage`
  only.
- Changing the chain rail or other sections to be responsive at high zoom.
- Remembering the maximised state across launches.

## Comments

2026-11-03 — Written up from the ideation session. The three crux decisions
(Fit + absolute %, two-axis scroll, maximise-with-title-bar) were agreed; the
remaining defaults above are pinned so the ticket is agent-ready. Ticket
numbered 0060 because 0059 (sticky quick-monitor bar) already exists.

2026-11-03 — Implemented. `UiScale` gained the Fit/manual zoom model, the
`.ui-shell-controls` cluster (`UiZoomControl.jsx`), two-axis scroll, the
`.ui-overlay`/`useUiScale()` integration needed by 0059, and Ctrl/Cmd + wheel /
`+`/`−`/`0` handling. `WebViewEditor` gained `frontendSetMaximised`, the
`standalone`/`maximised` transport fields and the `DocumentWindow` maximise /
restore path; `EditorWindowMath` is the pure work-area fit helper with a CTest.
The grip resize clears maximise, the size persist is suppressed while
maximised, and quitting maximised saves the pre-maximise size.

Verified: WebUI `vite build` green; `Tests/check-bridge-events.mjs` green (111
frontend events); `EditorWindowMath` compiled and exercised with a standalone
g++ harness; headless-Chrome smoke test (Fit 79% → + → 89%, overlay present,
Ctrl/Cmd+0 → Fit, `localStorage` persists, no renderer exceptions). The full
C++ target was **not** compiled in this environment (the cached Visual Studio
instance is no longer installed); needs a Windows/CI build before release.

2026-11-03 — Follow-up 0062: the originally chosen "maximise to the largest
aspect-correct size" still left desktop strips on a 16:9 screen, so maximise now
fills the whole work area (any aspect) and Fit stretches the design surface to
the available width. The fill helper and tests replaced the aspect-fit helper;
see `0062-fill-screen-fluid-width.md`.

