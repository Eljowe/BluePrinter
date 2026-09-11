---
id: "0024"
title: "Audio drag-drop and Import into the library"
status: ready-for-agent
blocked_by: []
---

# Audio drag-drop and Import into the library

## Problem

The library only gains audio by recording inside the plugin or by scanning the
library folder for `.wav` files (`refreshLibraryFromFolder`). There is no way to
bring in an arbitrary external file, so backing tracks, references, and audio
created elsewhere cannot sit alongside recorded takes.

## Current behaviour

`SnippetLibrary` can scan a folder and load `.wav` + optional `.json` sidecar, but
the UI exposes no import action, only folder selection and refresh.

## Required change

1. Add an **Import audio…** action (FileChooser, multi-select) and support
   dropping files onto the takes list / library area.
2. Decode with `juce::AudioFormatManager` (WAV/AIFF/FLAC/OGG/MP3 as the JUCE build
   provides) and add each via `SnippetLibrary::addSnippet`.
3. Handle sample-rate/channel differences sensibly (resample or document the
   behaviour) and compute peaks for the waveform.
4. Show progress while importing and notify on success/failure; skip unsupported
   or corrupt files with a clear message.
5. Avoid duplicate entries for a file already imported (track source path).

## Acceptance criteria

- Dropping a `.wav`/`.mp3` adds a playable snippet with a waveform, in both
  mono and stereo.
- Unsupported/corrupt files produce an error notification and are skipped, not
  crashing.
- Multi-file import works; re-importing the same file does not duplicate it.
- Imported snippets behave like recorded ones (rename, comments, colour, gain,
  save, reveal).

## Docs

`README.md` (snippet library), `AGENTS.md` (SnippetLibrary), `webui-bridge` skill.

## Files

`Source/SnippetLibrary.h/.cpp`, `Source/PluginProcessor.h/.cpp`,
`Source/WebViewEditor.h/.cpp`, `WebUI/src/components/SnippetList.jsx`,
`WebUI/src/bridge.js`, `CMakeLists.txt` (enable the needed `juce_audio_formats`
codecs).

## Out of scope

Video/container formats; automatic tempo/key analysis beyond the existing key
detection.
