# The looper is audio-only; no MIDI sequencing or `.mid` export

The looper captures and replays **audio** of the recording mix, and there is no
MIDI event recording, quantization, or `.mid` export. An earlier MIDI-listening
sequencer was deliberately removed. Chain hosts can still play instruments live
from incoming MIDI, but nothing sequences or exports that data; the snippet
library (WAV + JSON sidecar) is the only persistence story for a loop.

## Consequences

- Requests for `.mid` export are a new feature of significant size (capture,
  quantization, and a `juce::MidiFile` writer), not a small toggle — see ticket
  0026 for the open decision.
- Loop timing is expressed in beats/bars at the current BPM, not in MIDI events.
- Per-chain MIDI filtering affects live monitoring only.
