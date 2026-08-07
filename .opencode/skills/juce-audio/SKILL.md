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
2. Chains: copy the post-gain buffer into chainInputBuffer (pristine dry
   snapshot), then run every chain in parallel on the shared
   chainScratchBuffer — each copies its selected channels from
   chainInputBuffer, never from the accumulating mix, so chains can't hear
   each other. Each chain's output is summed into the main mix × its
   volume gain (unless muted). Chains with no active (non-bypassed)
   plugins are skipped entirely (hasActivePlugins) so a transparent chain
   doesn't double the dry signal. Each MIDI-listening chain gets its own
   copy of the input MIDI, filtered by its channel mask; the host's
   output MIDI stays raw input (+ clock events)
3. Looper capture tap: if the looper is armed, copy the POST-CHAIN buffer into
   the loop region of recordBuffer (bakes in synth sounds + FX; the click is
   mixed later so it never lands in the loop). In overdub mode
   (`looperOverdubCapture` set by the message thread at capture start) the tap
   instead writes the layer into the region after the existing loop
   (`overdubWritePos` — `audioLoopLength` stays fixed so the loop's wrap
   boundary never moves mid-capture); on stop, `mixOverdubLayer` wrap-mixes the
   layer into the loop on the message thread under `recordLock` (playback is
   stopped first so the mix can't race the audio thread's unlocked loop reads).
4. Record the take-recorder tap (post-gain, pre-click) under recordLock
5. Compute input levels (RMS + peak) from the clean signal
6. Audio loop playback: addFrom the cropped window
   [audioLoopStart, audioLoopStart + audioLoopLength) of recordBuffer, with a
   precomputed crossfade at the wrap point — post-chain, mixed over the live
   input, so the already-processed loop audio isn't double-processed
7. Snippet playback: substitute the recorded buffer in place of live input
   (does NOT re-run the chains); pending-take review playback renders
   similarly (renderTakePlayback, one-shot, playback-volume-scaled)
8. Looper count-in pre-roll: render the click, advance the beat clock, flip
   into capture when the configured beats elapse
9. Take-recorder pre-roll: same flow, flips into actual recording
10. Click during take recording + advance the continuous beat clock
    (click gated on metronomeEnabled AND clickDuringCapture — the
    header-level gate defaults on; when off the click only plays
    during the count-in; looper capture uses the same gate)
11. Click during looper capture + advance the same beat clock
12. MIDI clock output (24 ppqn from metronomePosition) + flush pending
    MIDI Start / Stop. Two header-level toggles drive it: `midiClockEnabled`
    is the master on/off; with `midiClockOnRecord` off the clock free-runs
    — advances `metronomePosition` itself and renders the audible click
    (subject to `metronomeEnabled`) — and takes / loop captures ride the
    same clock, re-syncing with a Start at actual-recording/capture time
    (`midiStartPending`). With `midiClockOnRecord` on the clock runs only
    while a take or loop capture is active (count-in included) and stops
    when the capture ends; the run condition is the atomics-only helper
    `wantsClockRun()`. Edges send Start/Stop directly to the hardware
    output device (`sendDirectMidiStart`/`sendDirectMidiStop` +
    `midiOutputLock`) — the standalone never forwards the host MIDI
    buffer to hardware, so without the direct send the drum machine stays
    silent. The click is gated header-level too: `metronomeEnabled`
    (master) AND `clickDuringCapture` (off = count-in only) for both take
    recording and looper capture.
```

The take recorder and the audio looper share `recordBuffer`; `startRecording`
and `setLooperRecording` preempt each other so they never capture
simultaneously. Both loops are otherwise independent of the metronome/clock.

**MIDI input limitation**: in the standalone, all MIDI input devices are
merged by the JUCE host into the single `MidiBuffer` that `processBlock`
receives — `AudioProcessorPlayer::handleIncomingMidiMessage` drops the
`MidiInput*`, and JUCE 8 `MidiMessage` has no source/device ID field. The
MIDI channel is the only per-message discriminator; per-chain device routing
is not possible without a custom standalone app main
(`JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP`). Per-chain channel filtering lives
in `PluginChain::processBlock` (`midiChannelsMask`, channels 1–16, system
messages always pass).

## Threading Model

- **Audio thread**: `processBlock()`, `prepareToPlay()`, `releaseResources()`. No locks, no allocations, no UI calls.
- **Message thread**: All GUI operations, chain mutations, file I/O, parameter changes.
- **Async:** `juce::AsyncUpdater` (WebViewEditor) and `juce::ThreadPool` for VST3 scanning.
- **Atomic flags**: `std::atomic<bool>` for audio-thread-visible state like `recordingRequested`, `playbackActive`.

## Plugin Chains (`PluginChain.h/.cpp`)

```cpp
class PluginChain {
    String chainId;                // stable id ("chain0", ...), survives rename/reorder
    String name;                   // user-editable
    std::vector<ChainSlot> slots;  // ChainSlot { unique_ptr<AudioPluginInstance>, bypassed, name, path }
    std::vector<PendingSlot> pendingSlots;  // deferred-restore queue, loaded one per timer tick
    Vst3Library& library;          // shared reference, not owned
    std::atomic<bool> wantsMidi;   // true: plugins get the live MIDI buffer; false: empty buffer
    std::atomic<float> volumeDb;   // -60..+12 dB, applied to the summed output unless muted
    std::atomic<bool> muted;       // still runs plugins, contributes nothing
    std::atomic<bool> recordOnCapture;  // output lands in recordingMixBuffer when on
    uint32_t midiChannelsMask;     // channels 1-16; system messages always pass
};
```

Key methods:
- `processBlock(buffer, midi)` — runs through non-bypassed plugins sequentially; passes a local empty `MidiBuffer` when `wantsMidi` is off (audio-thread read, message-thread set); filters MIDI by `midiChannelsMask` into `chainMidiFiltered` when needed
- `hasActivePlugins()` — false when every slot is bypassed/empty; such chains are skipped entirely in `processBlock` (a transparent chain's scratch would just be a dry copy, and summing it back would double the dry signal)
- `addPlugin(file, error)` — sync load
- `addPluginAsync(file, callback)` — async load on worker thread
- `prepareToPlay(sampleRate, blockSize)` — forwards to all plugins
- `popPendingSlot()` / `hasPendingSlots()` / `getNumPendingSlots()` — the deferred-restore queue; the processor's `timerCallback` pops one slot per message-loop turn and loads it
- Serializes state as a `juce::DynamicObject` (per-slot path, bypass flag, base64 plugin state, plus the chain's `wantsMidi`/`volume`/`muted`/`recordOnCapture`/`midiChannels` flags); the processor stores it as JSON under the `pluginChains` state property (`makeChainState()` / `applyChainState()` in `PluginProcessor.cpp`)
- Serializes state as a `juce::DynamicObject` for the WebView (includes a `pending` count of queued slots)

## Snippet Library (`SnippetLibrary.h/.cpp`)

```cpp
struct Snippet { int id; String name; String comments; String color; shared_ptr<const AudioBuffer<float>> audio; ... };

class SnippetLibrary {
    std::vector<std::shared_ptr<Snippet>> snippets;
    std::mutex mutex;  // protects all operations
};
```

Thread-safe behind a mutex. Supports:
- `addSnippet()` — creates from audio buffer, assigns incrementing ID
- `removeSnippet()`, `updateMeta()`, `markSaved()`, `setColor()` (`frontendSetSnippetColor`)
- `findById()`, `saveToWav()`, `loadFromWav()` (16-bit, with JSON sidecar — the sidecar stores the `color` field; snippets saved before it existed read back without one)
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

## Deferred Chain Restore (startup + host state loads)

`setChainState` never instantiates plugins — saved slots are queued as `PendingSlot`s (path, bypass, base64 state blob) and the processor's `timerCallback` (the `pendingPluginLoads`/`restoreActive` driver, fired from the 30 Hz `transportTimerHz` timer started in `prepareToPlay`) loads them ONE per message-loop turn via `addPluginAsync` (30 s timeout per slot), applying bypass + saved state in the load callback. This is deliberate: synchronously instantiating several plugins in a row kept the message thread inside plugin code, so window messages the plugins queue during setup got dispatched reentrantly and crashed some plugins (Neural DSP "X" amp sims heap-faulted). One slot per loop turn gives every plugin an idle gap.

- `isChainRestoreInProgress()` is true while `restoreActive`, while a load is in flight (`pendingPluginLoads != 0`), or while any chain has pending slots.
- **Persist guards**: `flushPendingChainPersist` refuses to write while a restore is in progress (keeps the arm pending and retries each tick); `getStateInformation` omits the `pluginChains` property entirely — the standalone writes its state blob on exit, so quitting mid-restore would otherwise capture the partial state and the next launch would restore *that* empty state.
- `chainRestoreCrashed` properties-file marker: if a launch crashes mid-restore, the next launch skips state-blob restore (plugins load with defaults). Crash diagnostics: `SetUnhandledExceptionFilter` writes `%APPDATA%\Retrokielto\crash-info.txt` via `setCrashOp`/`getCrashOp`.
- The frontend shows restore progress: per-chain `pending` counts + a `restoring` flag in `backendVst3Chain`, rendered by `SplashScreen.jsx` on startup.

## Parameters (APVTS)

Currently the APVTS exposes **`Gain`** (`AudioParameterFloat`, range 0.0–1.0, step 0.01, default 0.7) and **`PlaybackVolume`**, defined in `createParameterLayout()` in `PluginProcessor.cpp`. The metronome, BPM, count-in, MIDI-clock, and MIDI-device settings are **not** APVTS parameters — they live in standalone user state (`juce::PropertiesFile`) and `std::atomic` audio-thread flags. Add them there, not to the APVTS.

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
