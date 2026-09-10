import React from "react";
import { createRoot } from "react-dom/client";
import App from "./App";
import UiScale from "./components/UiScale";
import "./styles.css";

// WebView2 handles Ctrl+wheel / Ctrl +-  as page zoom by default, and JUCE
// exposes no option to disable it. Page zoom scales the CSS pixel, which
// would change the corner grip's visual size while the self-scaling UI
// stays put (UiScale compensates). Suppress the zoom gestures so the grip
// stays a constant size. Pinch-zoom arrives as ctrl+wheel on trackpads too.
const suppressCtrlWheelZoom = (event) => {
  if (event.ctrlKey || event.metaKey) event.preventDefault();
};
window.addEventListener("wheel", suppressCtrlWheelZoom, { passive: false });

const suppressCtrlKeyZoom = (event) => {
  if ((event.ctrlKey || event.metaKey) && ["+", "=", "-", "_", "0"].includes(event.key))
    event.preventDefault();
};
window.addEventListener("keydown", suppressCtrlKeyZoom, { passive: false });

createRoot(document.getElementById("root")).render(
  <React.StrictMode>
    <UiScale>
      <App />
    </UiScale>
  </React.StrictMode>,
);
