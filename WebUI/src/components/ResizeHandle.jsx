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

  // Keyboard resize: the grip is a real button so a keyboard user can grow
  // or shrink the window with the arrow keys (the aspect ratio is locked in
  // C++, so only the horizontal delta matters).
  const onKeyDown = useCallback((event) => {
    let direction = 0;
    if (event.key === "ArrowRight" || event.key === "ArrowUp") direction = 1;
    else if (event.key === "ArrowLeft" || event.key === "ArrowDown") direction = -1;
    if (direction === 0) return;
    event.preventDefault();
    const dpr = window.devicePixelRatio || 1;
    emit(FRONTEND_EVENTS.resizeEditor, { dWidth: direction * 24 * dpr });
  }, []);

  return (
    <button
      type="button"
      className="bp-resize-handle"
      aria-label="Resize window; drag or use the arrow keys"
      title="Drag, or use the arrow keys, to resize"
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={endDrag}
      onPointerCancel={endDrag}
      onKeyDown={onKeyDown}
      onLostPointerCapture={() => {
        drag.current.active = false;
      }}
    >
      <span className="bp-resize-grip" aria-hidden="true" />
    </button>
  );
}
