# Windows-only with a WebView2 UI

BluePrinter targets Windows 10/11 only and renders its UI in an embedded WebView2
(Edge) browser hosting a React + Vite app. The product is built around hosting
third-party VST3 amp sims and a WebView2 UI, both of which make cross-platform
support a large tax with no near-term payoff. The installer checks for the
WebView2 Runtime and installs it only when missing; when the runtime is absent the
editor falls back to a plain native message.

## Consequences

- Build scripts, the installer, and crash tooling are Windows-specific by design.
- A non-Windows build is not a supported target, so "it doesn't build on macOS"
  is not a bug.
- The WebView2 runtime dependency is installed by the installer, not assumed.
