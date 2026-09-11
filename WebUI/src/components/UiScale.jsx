import React, { useCallback, useEffect, useRef, useState } from "react";
import ResizeHandle from "./ResizeHandle";

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
// Because the ratio is locked, width and height yield the same scale
// factor; taking the min keeps the UI whole if a host ever reports an
// off-ratio size (the shortfall is centered by `offsetX`).
const DESIGN_WIDTH = 960;
const DESIGN_HEIGHT = 700;

export default function UiScale({ children }) {
  const scrollRef = useRef(null);
  const contentRef = useRef(null);
  const [{ scale, offsetX, minHeight }, setLayout] = useState({
    scale: 1,
    offsetX: 0,
    minHeight: DESIGN_HEIGHT,
  });
  const [contentHeight, setContentHeight] = useState(DESIGN_HEIGHT);

  // Measure the scroll container, NOT window.innerWidth: innerWidth
  // includes the vertical scrollbar, which would make the scaled content
  // a scrollbar-width too wide. clientWidth/clientHeight exclude it, and
  // (.ui-scroll reserves a stable gutter) stay constant as content grows.
  const measure = useCallback(() => {
    const el = scrollRef.current;
    const width = (el && el.clientWidth) || window.innerWidth || DESIGN_WIDTH;
    const height = (el && el.clientHeight) || window.innerHeight || DESIGN_HEIGHT;
    const nextScale = Math.min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT) || 1;

    setLayout({
      scale: nextScale,
      offsetX: Math.max(0, (width - DESIGN_WIDTH * nextScale) / 2),
      // minHeight is in the content's own coordinate space, so dividing by
      // the scale renders to exactly the viewport height — keeps short
      // content filling the window (viewport units are not used: the
      // content is transformed, not reflowed).
      minHeight: height / nextScale,
    });
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

  return (
    <div className="ui-viewport">
      <div className="ui-scroll" ref={scrollRef}>
        <div className="ui-scaled" style={{ height: `${contentHeight * scale}px` }}>
          <div
            className="ui-content"
            ref={contentRef}
            style={{
              minHeight: `${minHeight}px`,
              transform: `translateX(${offsetX}px) scale(${scale})`,
            }}
          >
            {children}
          </div>
        </div>
      </div>
      <ResizeHandle />
    </div>
  );
}
