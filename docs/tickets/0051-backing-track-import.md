---
id: "0051"
title: "Backing-track import with tempo/key detection"
status: needs-triage
blocked_by: []
---

# Backing-track import with tempo/key detection

## Problem

There is no way to jam or write against an imported track. The looper, click and
BPM are all independent of any reference audio.

## Current behaviour

- `SnippetLibrary::importAudioFile` decodes WAV/AIFF/FLAC/OGG/MP3 into the
  library; `KeyDetector::detectKey` estimates a snippet's key off the message
  thread.
- There is no tempo/BPM estimation, no backing-track concept, and no session BPM
  derived from audio.

## Open questions (resolve before implementation)

1. Where does the backing track live: a normal library snippet flagged as a
   backing track, or a separate session slot?
2. Signal path: monitor only (the "monitor controls never change the print"
   invariant) — or also available to the capture bus as a stem?
3. Detection: a tempo estimator (onset/autocorrelation) is new work; is a rough
   estimate + manual nudge acceptable for v1?
4. On import, do we offer to set the session BPM/key, and trim the loop length
   to detected bars?
5. Does tempo-locking the backing track need 0052 (time-stretch), or is
   play-at-native-tempo enough for v1?

## Required change (proposed, pending answers)

- A tempo estimator (pure, unit-tested) and reuse of `KeyDetector` on import;
  surface the estimates in the import flow.
- A session backing-track slot rendered in the monitor, with play/stop and a
  level.
- Optional: prefilter for BPM/key in the library.

## Acceptance criteria

TBD once the open questions are answered.

## Docs

`CONTEXT.md` (new term), `AGENTS.md` (library/import), `juce-audio` skill.

## Files

`Source/SnippetLibrary.*`, new `Source/TempoDetector.h` + tests,
`Source/PluginProcessor.*`, `Source/WebViewEditor.*`,
`WebUI/src/components/…`, `WebUI/src/bridge.js`.

## Out of scope

Cloud/streaming tracks; copyright handling.
