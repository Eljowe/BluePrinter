---
id: "0034"
title: "Built-in tuner"
status: ready-for-agent
blocked_by: []
---

# Built-in tuner

## Problem

BluePrinter is a guitar-first tool, but tuning means reaching for a separate tuner
app or pedal. A tuner fed by the input the plugin already meters is the obvious
quality-of-life win.

## Current behaviour

The post-gain, pre-chain input is already measured by the input meter
(`computeLevels(chainInputBuffer, …)`), but no pitch information is derived from it.
`KeyDetector` does chroma / Krumhansl-Schmuckler key-profile correlation — it yields a
*key* and pitch classes, not a fundamental with cents — so it cannot drive a tuner.

## Decisions (grilled)

- **Algorithm:** a new **pure, unit-tested monophonic pitch detector** (YIN, or
  autocorrelation with parabolic interpolation). Mix the input to mono, use a
  ~4096-sample window, and gate on RMS.
- **Source:** the post-gain, pre-chain/loop/click clean input (the same
  `chainInputBuffer` tap the input meter uses). Never post-chain.
- **UI:** a header popover (like the Keyboard help popover), not a third recording tab.
  Its open/closed state is remembered across launches.
- **Mute while tuning:** a monitor-only toggle in the tuner panel, **off by default**,
  **session-only** (resets each launch). It must never change the capture.
- **Reference pitch:** selectable (432/435/438/440/441/442), default **440**,
  persisted as standalone user state + atomic (per convention, **not** an APVTS
  parameter).
- **Threading:** no analysis on the audio thread. `processBlock` copies the mono
  post-gain input into a small lock-free window; one **long-lived worker thread** (run
  only while the popover is open) runs the detector and publishes
  frequency/confidence atomics. The reading rides the existing **30 Hz transport
  snapshot**; the frontend damps the needle. Silence/decay shows "—", never stale.

## Agent Brief

**Category:** enhancement
**Summary:** Add a built-in guitar tuner fed by the clean post-gain input, shown in a
header popover, with a selectable reference pitch and an optional monitor-only mute.

**Current behavior:**
The clean post-gain input is metered but not pitch-analysed. There is no tuner UI, no
reference-pitch setting, and no monitor mute for tuning.

**Desired behavior:**
While the tuner popover is open, continuously estimate the monophonic input's
fundamental and show note name + cents deviation with a damped needle.
- The estimate uses the post-gain, pre-chain input; it is unaffected by Output/master,
  loop playback, chain solo/mute, and recording.
- Below the RMS gate (silence/decay) the display shows "—" rather than the last note.
- The reference pitch selector changes the reported note/cents and persists across
  restart; A4 = 440 is the default.
- The monitor-only mute toggle silences the monitor while on and never changes what is
  captured; it resets to off on launch and is visibly indicated while active.
- The popover's open/closed state persists across restart. No pitch analysis runs while
  it is closed.
- Analysis happens off the audio thread; the UI updates at the 30 Hz transport cadence.

**Key interfaces:**
- A new pure analysis type that maps a mono sample block + sample rate + reference pitch
  to `{ frequency, confidence }` (or note + cents), unit-testable with synthetic tones.
- The processor owns a lock-free mono input window fed from the post-gain input, and a
  long-lived analysis worker started/stopped by the popover-open state; it publishes
  frequency/confidence atomics.
- The transport snapshot gains the tuner reading (frequency, confidence, note, cents)
  and the monitor-mute state.
- New frontend→backend events to set the reference pitch and toggle the monitor mute;
  registered in the shared event constants and mirrored in the WebUI bridge (parity
  test must pass).
- Reference pitch is persisted user state; monitor-mute is session-only.

**Acceptance criteria:**
- [ ] A real in-tune low E and high E read within a few cents; the needle is stable.
- [ ] Silence/decay shows "—", never a stale reading.
- [ ] The reading is independent of Output/master and loop playback.
- [ ] The monitor mute silences only the monitor; a simultaneous capture is byte-for-byte
      unchanged by it.
- [ ] Changing the reference pitch changes the reported note/cents and survives restart.
- [ ] Popover open/closed state survives restart; no analysis runs while closed.
- [ ] No allocations or locks are added to the audio callback.
- [ ] Unit tests cover the pitch math against known-frequency synthetic input.

## Docs

`AGENTS.md` (metering/input tap, standalone user state vs APVTS), `README.md`,
`juce-audio` and `webui-bridge` skill notes.

## Files

new `Source/Tuner.h/.cpp` (pure math + worker), `Source/PluginProcessor.h/.cpp`,
`Source/WebViewEditor.h/.cpp`, new `WebUI/src/components/Tuner.jsx`,
`WebUI/src/components/HeaderControls.jsx`, `WebUI/src/App.jsx`,
`WebUI/src/bridge.js`, `Tests/`.

## Out of scope

Polyphonic/chord detection; tuning history; auto-mute; exposing reference pitch as an
APVTS parameter; reference pitches beyond the listed set.
