---
id: "0029"
title: "Audio-path render modules + golden tests"
status: done
blocked_by: ["0012"]
---

# Audio-path render modules + golden tests

## Problem

Every test in `Tests/` is pure scalar math (key detection, gain/normalize,
grid quantization, click synthesis/scheduling, meter math). The actual audio
**render** — loop playback with its seam de-click and wrap, the overdub
wrap-mix, the capture writes — is inline in `processBlock` and has no
automated coverage. A wrong wrap point, a missing fade, or an off-by-one in
the capture is only ever caught by ear, and the remaining 0027 extractions
(6b/6c) have no safety net.

## Current behaviour

- Loop playback is ~30 lines of per-sample DSP inside `processBlock` step 6:
  reads `recordBuffer[start + position)`, applies a seam de-click and the
  Loop level, wraps the phase, tracks a playback peak, and stops one-shot
  loops.
- The take-overdub monitor playback (step 6b) is a near-copy of the same
  loop read.
- `mixOverdubLayer` does the pedal-style wrap-mix of a layer region into the
  loop window.
- All of it touches `recordBuffer`, so it can only be exercised through the
  whole processor (which cannot be linked into the headless test target —
  `PluginProcessor.cpp` pulls in WebView2 and the Win32 crash handler).

## Required change

Extract the pure buffer-render steps into modules operating on
`juce::AudioBuffer<float>`, and cover them with headless golden tests:

1. `LoopPlayback::render(dest, source, start, length, position, looping,
   gain, declick, outBlockPeak)` — additive render of one block with the
   seam de-click, phase wrap, one-shot stop, and the post-gain block peak.
2. `LoopPlayback::mixLayer(buffer, loopStart, loopLength, layerBase,
   layerLength, gain)` — the wrap-mix used by the looper and take overdubs.
3. `ChainRouting::copyInputChannels(dest, source, mask, numChannels,
   numSamples)` — a chain's selected-channel scratch build; and
   `ChainRouting::sumInto(dest, source, gain, numSamples)` — the monitor /
   record-bus sum.
4. `CaptureWrite::write(dest, start, source, numSamples, limit)` — the
   clamped capture-buffer write shared by the take recorder and the loop tap.

`processBlock` calls them (loop playback, take-overdub playback,
`mixOverdubLayer`, the chain scratch/mix, the loop capture tap, and
`writeRecording`) so the DSP has one definition and is tested directly.

## Acceptance criteria

- `Tests/test_LoopPlayback.cpp`, `Tests/test_ChainRouting.cpp` and
  `Tests/test_CaptureWrite.cpp` cover, at minimum: a one-shot block, a
  wrapping block across a cycle boundary, the one-shot early stop, the
  de-click envelope at the seam, the reported block peak, the wrap-mix with
  and without a partial second cycle, gain scaling, invalid input; selected
  vs missing channels and the monitor/record sums; and the capture write's
  full-block, clamped, full-buffer and shared-channel cases.
- `processBlock`'s loop playback, take-overdub playback, chain scratch/mix,
  loop capture tap and take recorder produce the same samples as before
  (behaviour-preserving).
- `ctest` stays green and the WebUI/plugin targets still build in CI.

## Docs

`CONTEXT.md` (module map), `README.md` (Testing), `AGENTS.md` if the render
steps are described there.

## Files

`Source/LoopPlayback.h`, `Source/ChainRouting.h`, `Source/CaptureWrite.h`
(new), `Source/PluginProcessor.cpp`, `Tests/test_LoopPlayback.cpp`,
`Tests/test_ChainRouting.cpp`, `Tests/test_CaptureWrite.cpp` (new),
`CMakeLists.txt`, `CONTEXT.md`, `README.md`.

## Out of scope

Instantiating the real `AudioProcessor` in the test target (it links WebView2
and Win32 crash handling); a full `processBlock` integration harness. This
ticket makes the render steps testable in isolation, not the whole processor.
