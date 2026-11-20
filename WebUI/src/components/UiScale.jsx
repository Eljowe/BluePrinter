import React, { useCallback, useEffect, useMemo, useRef, useState } from "react";
import ResizeHandle from "./ResizeHandle";
import UiZoomControl from "./UiZoomControl";

// The C++ editor window is resizable with a locked aspect ratio: its
// content area always matches DESIGN_WIDTH:DESIGN_HEIGHT. Rather than
// reflowing the whole page at small sizes, we lay the UI out once at the
// fixed design size and scale that whole surface to fill the window —
// the same model Neural DSP's plugins use.
//
// We scale with `transform: scale()` (NOT the CSS `zoom` property):
// Chromium's `zoom` is under-specified and mis-paints dynamically-updated
// content (pressing a control could leave the lower part of the page
// blank until a forced repaint), which is a serious problem for a live
// control surface. `transform` is well-tested; its two downsides are
// handled here — transforms don't affect layout (so .ui-scaled reserves
// the scaled scroll height via a ResizeObserver), and a transformed
// element becomes the containing block for `position: fixed` descendants
// (so the splash and the notification toast are portaled to <body>).
//
// Zoom (0060): the default is Fit (min(width/960, height/700), exactly the
// old behaviour). A manual zoom is absolute — 100% means the design surface
// at its real 960 CSS px width — so the percentage means the same thing at
// any window size. Below Fit the surface is centred and shows more vertical
// content; above Fit .ui-scroll scrolls in both directions. The zoom and
// maximise controls live outside the transformed content so they stay
// reachable and constant-size at every zoom level.
const DESIGN_WIDTH = 960;
const DESIGN_HEIGHT = 700;

const ZOOM_STORAGE_KEY = "blueprinter.uiZoom";
const ZOOM_MIN = 50;
const ZOOM_MAX = 300;
const ZOOM_STEP = 10;

// Shared with the app (0059): the live scale and a portal target in the
// fixed, pre-scaled overlay layer above the scrolling content.
const UiScaleContext = React.createContext(null);

export function useUiScale() {
  return React.useContext(UiScaleContext);
}

function clampZoom(value) {
  return Math.max(ZOOM_MIN, Math.min(ZOOM_MAX, Math.round(value)));
}

function readStoredZoom() {
  try {
    const raw = localStorage.getItem(ZOOM_STORAGE_KEY);
    if (raw === null || raw === "fit") return "fit";
    const value = Number(raw);
    if (!Number.isFinite(value)) return "fit";
    return clampZoom(value);
  } catch {
    return "fit";
  }
}

export default function UiScale({ children }) {
  const scrollRef = useRef(null);
  const contentRef = useRef(null);
  const [zoom, setZoom] = useState(readStoredZoom);
  const [viewport, setViewport] = useState({
    width: DESIGN_WIDTH,
    height: DESIGN_HEIGHT,
  });
  const [contentHeight, setContentHeight] = useState(DESIGN_HEIGHT);
  const [overlayNode, setOverlayNode] = useState(null);
  const [scrollLeft, setScrollLeft] = useState(0);
  const viewportRef = useRef(viewport);

  // Measure the scroll container, NOT window.innerWidth: innerWidth
  // includes the vertical scrollbar, which would make the scaled content
  // a scrollbar-width too wide. clientWidth/clientHeight exclude it, and
  // (.ui-scroll reserves a stable gutter) stay constant as content grows.
  const measure = useCallback(() => {
    const el = scrollRef.current;
    const width = (el && el.clientWidth) || window.innerWidth || DESIGN_WIDTH;
    const height = (el && el.clientHeight) || window.innerHeight || DESIGN_HEIGHT;
    viewportRef.current = { width, height };
    setViewport({ width, height });
  }, []);

  useEffect(() => {
    measure();

    const el = scrollRef.current;
    if (!el || typeof ResizeObserver === "undefined") {
      window.addEventListener("resize", measure);
      return () => window.removeEventListener("resize", measure);
    }

    const observer = new ResizeObserver(measure);
    observer.observe(el);
    return () => observer.disconnect();
  }, [measure]);

  // Track the unscaled content height so .ui-scaled can reserve the
  // correct scaled height (transforms don't affect layout).
  useEffect(() => {
    const el = contentRef.current;
    if (!el) return undefined;

    const update = () => setContentHeight(el.offsetHeight || DESIGN_HEIGHT);
    update();

    if (typeof ResizeObserver === "undefined") return undefined;

    const observer = new ResizeObserver(update);
    observer.observe(el);
    return () => observer.disconnect();
  }, []);

  useEffect(() => {
    const el = scrollRef.current;
    if (!el) return undefined;

    const onScroll = () => {
      setScrollLeft((previous) => (previous === el.scrollLeft ? previous : el.scrollLeft));
    };

    el.addEventListener("scroll", onScroll, { passive: true });
    return () => el.removeEventListener("scroll", onScroll);
  }, []);

  useEffect(() => {
    try {
      localStorage.setItem(ZOOM_STORAGE_KEY, String(zoom));
    } catch {
      // A private/blocked localStorage must not break the zoom control.
    }
  }, [zoom]);

  const stepZoom = useCallback((direction) => {
    setZoom((previous) => {
      const { width, height } = viewportRef.current;
      const fit = Math.min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT) || 1;
      const current = previous === "fit" ? Math.round(fit * 100) : previous;
      return clampZoom(current + direction * ZOOM_STEP);
    });
  }, []);

  const fitZoom = useCallback(() => setZoom("fit"), []);

  // Ctrl/Cmd + wheel and Ctrl/Cmd + - / = / 0 drive the UI zoom. Page zoom
  // is already suppressed in main.jsx; this turns those gestures into the
  // intentional one. Wheel events are throttled so trackpad pinch does not
  // race through the whole range in one gesture.
  useEffect(() => {
    let lastWheelAt = 0;

    const onWheel = (event) => {
      if (!(event.ctrlKey || event.metaKey) || event.altKey || event.deltaY === 0) return;
      event.preventDefault();
      const now = performance.now();
      if (now - lastWheelAt < 80) return;
      lastWheelAt = now;
      stepZoom(event.deltaY < 0 ? 1 : -1);
    };

    const onKeyDown = (event) => {
      if (!(event.ctrlKey || event.metaKey) || event.altKey) return;
      if (["+", "="].includes(event.key)) {
        event.preventDefault();
        stepZoom(1);
      } else if (["-", "_"].includes(event.key)) {
        event.preventDefault();
        stepZoom(-1);
      } else if (event.key === "0") {
        event.preventDefault();
        fitZoom();
      }
    };

    window.addEventListener("wheel", onWheel, { passive: false });
    window.addEventListener("keydown", onKeyDown, { passive: false });
    return () => {
      window.removeEventListener("wheel", onWheel);
      window.removeEventListener("keydown", onKeyDown);
    };
  }, [stepZoom, fitZoom]);

  const fitScale = Math.min(viewport.width / DESIGN_WIDTH, viewport.height / DESIGN_HEIGHT) || 1;
  const isFit = zoom === "fit";
  const scale = isFit ? fitScale : zoom / 100;

  // Fit uses the extra width: the surface is at least the design width and
  // otherwise fills the viewport, so a wide (or maximised) window gets more
  // columns/chain cards instead of paper gutters. Manual zoom stays absolute
  // — a 960-wide surface, centred or scrolled.
  const layoutWidth = isFit
    ? Math.max(DESIGN_WIDTH, viewport.width / scale)
    : DESIGN_WIDTH;
  const offsetX = Math.max(0, (viewport.width - layoutWidth * scale) / 2);
  const effectivePercent = Math.round(scale * 100);

  const contextValue = useMemo(
    () => ({ scale, offsetX, overlayNode }),
    [scale, offsetX, overlayNode],
  );

  return (
    <UiScaleContext.Provider value={contextValue}>
      <div className="ui-viewport">
        <div className="ui-scroll" ref={scrollRef}>
          <div
            className="ui-scaled"
            style={{
              width: `${Math.max(viewport.width, layoutWidth * scale)}px`,
              height: `${contentHeight * scale}px`,
            }}
          >
            <div
              className="ui-content"
              ref={contentRef}
              style={{
                width: `${layoutWidth}px`,
                // minHeight is in the content's own coordinate space, so
                // dividing by the scale renders to exactly the viewport
                // height — keeps short content filling the window (viewport
                // units are not used: the content is transformed, not
                // reflowed). Zooming out therefore shows more vertical
                // content instead of stretching the app.
                minHeight: `${viewport.height / scale}px`,
                transform: `translateX(${offsetX}px) scale(${scale})`,
              }}
            >
              {children}
            </div>
          </div>
        </div>
        {/* Fixed, pre-scaled overlay layer for the sticky quick bar (0059).
            It shares the zoom transform but not the scroll. */}
        <div className="ui-overlay">
          <div
            className="ui-overlay-scale"
            ref={setOverlayNode}
            style={{
              width: `${layoutWidth}px`,
              transform: `translateX(${offsetX - scrollLeft}px) scale(${scale})`,
            }}
          />
        </div>
        <UiZoomControl
          effectivePercent={effectivePercent}
          isFit={isFit}
          onStep={stepZoom}
          onFit={fitZoom}
        />
        <ResizeHandle />
      </div>
    </UiScaleContext.Provider>
  );
}
