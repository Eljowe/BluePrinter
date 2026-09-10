import React, { useCallback, useEffect, useRef, useState } from "react";
import ResizeHandle from "./ResizeHandle";

// The C++ editor window is resizable with a locked aspect ratio: its
// content area always matches DESIGN_WIDTH:DESIGN_HEIGHT. Rather than
// reflowing the whole page at small sizes, we lay the UI out once at the
// fixed design size and scale that whole surface to fill the window —
// the same model Neural DSP's plugins use.
//
// We scale with the CSS `zoom` property rather than `transform: scale()`:
// zoom is layout-aware (the scroll container sees the real scaled height,
// so no height bookkeeping is needed) and it does NOT turn the element
// into a containing block for `position: fixed` descendants — so the
// splash overlay stays anchored to the viewport instead of centering over
// the whole scrollable document. It also re-rasterizes text, so glyphs
// stay crisp at 2x.
//
// Because the ratio is locked, width and height yield the same scale
// factor; taking the min keeps the UI whole if a host ever reports an
// off-ratio size (the shortfall is centered by .ui-content's auto margins).
const DESIGN_WIDTH = 960;
const DESIGN_HEIGHT = 700;

export default function UiScale({ children }) {
  const scrollRef = useRef(null);
  const [{ scale, minHeight }, setLayout] = useState({ scale: 1, minHeight: DESIGN_HEIGHT });

  // Measure the scroll container, NOT window.innerWidth: innerWidth
  // includes the vertical scrollbar, which would make the scaled content
  // a scrollbar-width too wide. clientWidth/clientHeight exclude it, and
  // (.ui-scroll reserves a stable gutter) stay constant as content grows.
  const measure = useCallback(() => {
    const el = scrollRef.current;
    const width = (el && el.clientWidth) || window.innerWidth || DESIGN_WIDTH;
    const height = (el && el.clientHeight) || window.innerHeight || DESIGN_HEIGHT;
    const nextScale = Math.min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT) || 1;

    // minHeight is in the content's own (zoomed) coordinate space, so
    // dividing by the scale renders to exactly the viewport height — keeps
    // short content filling the window without relying on viewport units,
    // which are unreliable under zoom.
    setLayout({ scale: nextScale, minHeight: height / nextScale });
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

  return (
    <div className="ui-viewport">
      <div className="ui-scroll" ref={scrollRef}>
        <div
          className="ui-content"
          style={{ zoom: String(scale), minHeight: `${minHeight}px` }}
        >
          {children}
        </div>
      </div>
      <ResizeHandle />
    </div>
  );
}
