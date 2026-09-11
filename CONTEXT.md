# BluePrinter

A Windows guitar take-recorder plugin (VST3 + Standalone) that hosts parallel VST3
FX chains, records takes and loops from the processed signal, and files them in a
local snippet library. This file defines the project's language and shape; deep
implementation detail lives in [`AGENTS.md`](AGENTS.md).

## Language

### Capture and playback

**Take**:
A one-shot recording of the recording mix into the shared record buffer.
_Avoid_: Recording, clip

**Pending take**:
A captured take that has stopped but not yet been saved or discarded. Any new
capture invalidates it.
_Avoid_: Unsaved take, take buffer

**Loop**:
A bar-aligned, repeatable capture of the recording mix, optionally cropped to
whole beats.
_Avoid_: Loop region, backing track

**Recording mix** (also *capture mix*):
The signal actually printed: the dry input scaled by Dry, plus the outputs of
chains whose Record toggle is on. Takes and loops both tap it.
_Avoid_: Record bus, wet mix

**Dry**:
The direct, unprocessed input scaled by the Dry level, present in both the monitor
mix and the recording mix. Chains always receive the unscaled input.
_Avoid_: DI, clean signal

**Overdub**:
Recording a new layer over an existing loop instead of replacing it, mixed in on
stop by `mixOverdubLayer`.
_Avoid_: Layer, comp

**Count-in**:
A click-only pre-roll of N beats before a capture starts.
_Avoid_: Pre-roll (reserved for the processor's `preRollActive` state)

**Click**:
The audible metronome, gated by the master Click toggle and the Click: capture
toggle.
_Avoid_: Metronome (the APVTS-independent settings object is `metronomeEnabled`)

**Bounce**:
Materialising a loop as a new library snippet.
_Avoid_: Export (export writes a file; bounce creates a snippet)

### Routing

**Chain**:
An independently named, parallel VST3 processing path with its own input mask,
MIDI routing, Record toggle, volume, mute, and monitor state.
_Avoid_: Bus, track, strip

**Slot**:
One plugin instance within a chain, ordered and individually bypassable.
_Avoid_: Plugin, insert

**Input mask**:
The subset of the 1–8 channel input bus a chain reads. A chain never reads another
chain's output.
_Avoid_: Channel map

**Monitor mix**:
What the user hears, post-master-Output. Monitor-only Solo and monitor-mute change
it; they never change the recording mix.
_Avoid_: Main mix (that is the pre-Output sum)

**Blocklist** / **Quarantine**:
Two different exclusions. A *blocklist* entry is a plugin the user chose to skip
in the scanner. A *quarantine* entry is a plugin the self-heal added after it
crashed a restore or load.
_Avoid_: Blacklist for quarantine

### Library

**Snippet**:
An in-memory library item: audio plus metadata (name, comments, colour, key,
playback gain). A take or loop becomes a snippet when saved.
_Avoid_: Sample, item, take (a take is pre-save)

**Sidecar**:
The JSON file written beside a snippet's WAV holding its metadata.
_Avoid_: Metadata file

**Gain** (snippet):
A non-destructive playback trim in dB, never baked into the WAV.
_Avoid_: Normalize (that sets Gain so the peak lands at −1 dBFS)

**Tag** / **Colour**:
An organisational colour key on a snippet; the user-facing name of a colour is a
per-user Tag name stored once, not per snippet.
_Avoid_: Label, category

### Session and state

**Properties file**:
`%APPDATA%\Retrokielto\BluePrinter.properties` — BluePrinter's own user state
(chains, quarantine, metronome, editor size).
_Avoid_: Settings

**Settings file**:
`%APPDATA%\BluePrinter\BluePrinter.settings` — the JUCE standalone's own state
blob, written only on clean exit. Distinct from the properties file; deleting it
is safe.
_Avoid_: State file

**Deferred restore**:
Loading saved chain slots one plugin per message-loop turn, never synchronously.
_Avoid_: Lazy load

**Self-heal**:
Skipping state-blob restore and/or quarantining a plugin after a crash, so the
next launch does not repeat it.
_Avoid_: Recovery

### UI

**Design size**:
The fixed 960×700 layout the React UI is authored at.
_Avoid_: Base size

**UI scale**:
The single `transform: scale()` factor applied to the whole UI from the measured
scroll viewport. Never CSS `zoom`.
_Avoid_: Zoom

**Bridge event**:
A named frontend↔backend message, declared once in `WebViewEditor.h` and mirrored
in `bridge.js`.
_Avoid_: Message, IPC event

## Module map

| Area | Where |
| ---- | ----- |
| Audio processor, routing, state, restore | `Source/PluginProcessor.h/.cpp` |
| WebView2 editor + bridge dispatch | `Source/WebViewEditor.h/.cpp` |
| VST3 chain + async loading | `Source/PluginChain.h/.cpp` |
| VST3 scanning | `Source/Vst3Library.h/.cpp` |
| Snippet library + WAV/JSON | `Source/SnippetLibrary.h/.cpp` |
| Key detection | `Source/KeyDetector.h/.cpp` |
| Metronome click synthesis | `Source/ClickSynth.h/.cpp` |
| Metronome click scheduling + ring-out | `Source/MetronomePlayer.h/.cpp` |
| MIDI clock pulse scheduling | `Source/MidiClockMath.h` |
| Metering math | `Source/MeterMath.h` |
| React app + components | `WebUI/src/` |
| Bridge API | `WebUI/src/bridge.js` |

## Invariants

- The audio thread never allocates and never takes a lock inside `processBlock`.
- Chains run in parallel on a pristine input snapshot and never hear each other.
- Monitor controls (Output, Solo, monitor-mute, Loop level) never change the print.
- Restore is deferred one plugin per loop turn; persist is gated while it runs.
- Event names live once in `WebViewEditor.h` and once in `bridge.js`; they must match.
