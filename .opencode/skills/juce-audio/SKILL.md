---
name: juce-audio
description: Use ONLY when writing or modifying C++ JUCE audio plugin code for BluePrinter. Covers AudioProcessor, APVTS parameters, DSP processBlock, AudioProcessorEditor, WebView2 integration, threading model, and state persistence patterns.
---

# JUCE Audio Plugin Patterns

BluePrinter uses JUCE for audio processing, VST3 hosting, and WebView2 UI integration.

## AudioProcessor (`PluginProcessor.h/.cpp`)

- Inherits `juce::AudioProcessor` and `private juce::Timer`.
- APVTS: `juce::AudioProcessorValueTreeState apvts {*this, nullptr, "Parameters", createParameterLayout()}`. Always declared inline in the header. Parameters defined in `createParameterLayout()`.
- **Parameter access**: `apvts.getRawParameterValue("Gain")->load()` for audio thread reads.
- **Thread-safe communication**: `std::atomic` for flags read on the audio thread, set on the message thread.
- **State persistence**: `getStateInformation` / `setStateInformation` serialize to XML (`juce::XmlElement`). Standalone uses `juce::PropertiesFile` for user settings (window size, library folder, etc.).

## Audio Path (`processBlock`)

```
1. Apply gain parameter (apvts.getRawParameterValue("Gain")->load())
2. Run the MIDI chain on its own scratch copy of the post-gain buffer, sum its
   output back in; run the audio chain on the summed buffer (shared MIDI buffer)
3. Looper capture tap: if the looper is armed, copy the POST-CHAIN buffer into
   the loop region of recordBuffer (bakes in synth sounds + FX; the click is
   mixed later so it never lands in the loop)
4. Record the take-recorder tap (post-gain, pre-click) under recordLock
5. Compute input levels (RMS + peak) from the clean signal
6. Audio loop playback: addFrom the cropped window
   [audioLoopStart, audioLoopStart + audioLoopLength) of recordBuffer, with a
   precomputed crossfade at the wrap point — post-chain, mixed over the live
   input, so the already-processed loop audio isn't double-processed
7. Snippet playback: substitute the recorded buffer in place of live input
   (does NOT re-run the chains)
8. Looper count-in pre-roll: render the click, advance the beat clock, flip
   into capture when the configured beats elapse
9. Take-recorder pre-roll: same flow, flips into actual recording
10. Click during take recording + advance the continuous beat clock
11. Click during looper capture + advance the same beat clock
12. MIDI clock output (24 ppqn from metronomePosition) + flush pending
    MIDI Start / Stop
```

The take recorder and the audio looper share `recordBuffer`; `startRecording`
and `setLooperRecording` preempt each other so they never capture
simultaneously. Both loops are otherwise independent of the metronome/clock.

## Threading Model

- **Audio thread**: `processBlock()`, `prepareToPlay()`, `releaseResources()`. No locks, no allocations, no UI calls.
- **Message thread**: All GUI operations, chain mutations, file I/O, parameter changes.
- **Async:** `juce::AsyncUpdater` (WebViewEditor) and `juce::ThreadPool` for VST3 scanning.
- **Atomic flags**: `std::atomic<bool>` for audio-thread-visible state like `recordingRequested`, `playbackActive`.

## Plugin Chains (`PluginChain.h/.cpp`)

```cpp
class PluginChain {
    std::vector<ChainSlot> slots;  // ChainSlot { unique_ptr<AudioPluginInstance>, bypassed, name, path }
    Vst3Library& library;          // shared reference, not owned
};
```

Key methods:
- `processBlock(buffer, midi)` — runs through non-bypassed plugins sequentially
- `addPlugin(file, error)` — sync load
- `addPluginAsync(file, callback)` — async load on worker thread
- `prepareToPlay(sampleRate, blockSize)` — forwards to all plugins
- Serializes state as a `juce::DynamicObject` (per-slot path, bypass flag, base64 plugin state); the processor stores it as JSON under the `pluginChains` state property (`makeChainState()` / `applyChainState()` in `PluginProcessor.cpp`)
- Serializes state as a `juce::DynamicObject` for the WebView

## Snippet Library (`SnippetLibrary.h/.cpp`)

```cpp
struct Snippet { int id; String name; String comments; shared_ptr<const AudioBuffer<float>> audio; ... };

class SnippetLibrary {
    std::vector<std::shared_ptr<Snippet>> snippets;
    std::mutex mutex;  // protects all operations
};
```

Thread-safe behind a mutex. Supports:
- `addSnippet()` — creates from audio buffer, assigns incrementing ID
- `removeSnippet()`, `updateMeta()`, `markSaved()`
- `findById()`, `saveToWav()`, `loadFromWav()` (16-bit, with JSON sidecar)
- `computePeaks()` — downsampled waveform display data

## WebView2 Editor (`WebViewEditor.h/.cpp`)

- Inherits `juce::AudioProcessorEditor`, `APVTS::Listener`, `BluePrinterAudioProcessor::Listener`, `juce::AsyncUpdater`, `juce::Timer`.
- Constructor: creates `juce::WebBrowserComponent` with `WebBrowserComponent::Options`, registers event listeners.
- Registering a new frontend event listener pattern:
  ```cpp
  options.withEventListener(frontendSomeEvent, [this](const var& data) { handleSomeEvent(data); });
  ```
- Emitting to JS: `webView->emitEventIfBrowserIsVisible(backendSomeEvent, payload);`
- The initialization blob: `withInitialisationData("parameters", ...)` and `withInitialisationData("snippets", ...)` and `withInitialisationData("transport", ...)`.

## Parameters (APVTS)

Currently the APVTS exposes only **`Gain`** (`AudioParameterFloat`, range 0.0–1.0, step 0.01, default 0.7), defined in `createParameterLayout()` in `PluginProcessor.cpp`. The metronome, BPM, count-in, MIDI-clock, and MIDI-device settings are **not** APVTS parameters — they live in standalone user state (`juce::PropertiesFile`) and `std::atomic` audio-thread flags. Add them there, not to the APVTS.

## Adding a New Parameter

1. Add an entry in `createParameterLayout()` (defined in `PluginProcessor.cpp`):
   ```cpp
   layout.add(std::make_unique<juce::AudioParameterFloat>(paramId, "Display Name",
       juce::NormalisableRange<float>(min, max, step), defaultValue));
   ```
2. Read it in `processBlock`: `apvts.getRawParameterValue(paramId)->load()`.
3. Add it to the parameter snapshot sent to the frontend in WebViewEditor's timer callback.
4. Add a CSS class or UI component that reads/writes it via `setParameter` + `backendParameters`.

## Don't

- Don't allocate, lock, or call UI APIs from `processBlock` / `prepareToPlay` / `releaseResources`.
- Don't add metronome / BPM / count-in / MIDI-clock settings to APVTS — use standalone user state + atomics.
- Don't introduce raw owning pointers — use `std::unique_ptr` (or `std::shared_ptr` for shared snippet audio, as `SnippetLibrary` already does).
