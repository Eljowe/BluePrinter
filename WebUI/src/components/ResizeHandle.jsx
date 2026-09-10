import { useCallback, useRef } from "react";
import { emit, FRONTEND_EVENTS } from "../bridge";

// Neural-DSP-style corner grip. The plugin window has no OS/host resize
// border (JUCE would draw its own corner, but WebView2 is a native child
// window and occludes JUCE components), so this HTML grip is the only
// resize affordance: dragging it emits device-pixel width deltas and the
// C++ editor applies the locked aspect ratio + size limits.
//
// screenX (not clientX) is used because the window resizes under the
// pointer — client coordinates would feed the resize back into itself.
export default function ResizeHandle() {
  const drag = useRef({ active: false, lastX: 0 });

  const onPointerDown = useCallback((event) => {
    if (event.button !== 0) return;
    drag.current = { active: true, lastX: event.screenX };
    event.currentTarget.setPointerCapture(event.pointerId);
    event.preventDefault();
  }, []);

  const onPointerMove = useCallback((event) => {
    const state = drag.current;
    if (!state.active) return;

    const dx = event.screenX - state.lastX;
    if (dx === 0) return;
    state.lastX = event.screenX;

    const dpr = window.devicePixelRatio || 1;
    emit(FRONTEND_EVENTS.resizeEditor, { dWidth: dx * dpr });
  }, []);

  const endDrag = useCallback((event) => {
    if (!drag.current.active) return;
    drag.current.active = false;
    if (event.currentTarget.hasPointerCapture?.(event.pointerId))
      event.currentTarget.releasePointerCapture(event.pointerId);
  }, []);

  return (
    <button
      type="button"
      tabIndex={-1}
      className="bp-resize-handle"
      aria-label="Resize window"
      title="Drag to resize"
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={endDrag}
      onPointerCancel={endDrag}
      onLostPointerCapture={() => {
        drag.current.active = false;
      }}
    >
      <span className="bp-resize-grip" aria-hidden="true" />
    </button>
  );
}
