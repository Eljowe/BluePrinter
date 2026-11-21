# Host-reported plugin delay compensation with a capture cursor offset

The parallel chains and the dry pass-through are latency-aligned before they are
summed. Each chain's latency is the sum of `getLatencySamples()` over its loaded,
**non-bypassed** slots (a bypassed slot counts as 0; a chain with no active
plugins reports 0). One global `L` is the maximum over the loaded, non-bypassed,
**non-hard-muted** chains — monitor solo and monitor-mute are monitoring-only
and never change it. The dry path is delayed by `L` and each chain by
`L - its latency`, so both the monitor mix and the recording (print) mix sum
phase-coherently. A chain whose latency is below the maximum waits; the slowest
source waits nothing. `L == 0` is a bit-identical no-op.

The alignment is capped at **500 ms** at the session rate. A source past the cap
(and therefore `L`) is clamped, and a single non-repeating notification reports
it; there is no silent truncation. The delay lines (one for the dry path plus
one per chain) are preallocated on the message thread to the cap, and the audio
thread only reads/writes them. A latency change is a discontinuity: the
affected line is cleared when its delay changes.

## Host report

`L` is reported to the host with `AudioProcessor::setLatencySamples(L)` whenever
it changes — only from the message thread (the 30 Hz `timerCallback`), never
from `processBlock`. A DAW can therefore shift its timeline / project PDC by the
plugin's real internal latency instead of leaving the captured audio late
against the DAW grid.

## Latching during a capture

While any take or loop capture is active (count-in included), `L` is **latched**
for the duration of that capture. A plugin latency change mid-capture does not
move the alignment of the audio already being printed; the pending new `L`
applies once the capture ends.

## Capture cursor offset

Because the print is delayed by `L`, a capture skips the first `L` frames of the
delayed mix before it starts writing, so the saved take/loop begins on the armed
downbeat rather than `L` samples late. The skip is consumed only by frames the
capture actually writes. Fixed-length auto-stop counts **written** samples, so
exactly N bars are captured; grid trim, crop and overdub geometry are unchanged.
The per-chain stems (0038) share the same cursor offset and the same aligned
sources, so they still sum to the captured mix and have the captured length.

## Consequences

- Monitoring is `L` samples late; the player hears the aligned mix while
  tracking. This is accepted in exchange for a coherent monitor and print.
- The click, loop playback, take-overdub monitor, snippet playback, take-review
  playback and the directly-sent MIDI clock are **not** delayed. They stay on
  the true (undelayed) grid and are the references the player follows.
- The input meter and the tuner keep tapping the pristine, undelayed post-gain
  snapshot; chain output meters stay pre-delay and post-volume.
- No new bridge event and no on-disk state change: the cap warning reuses the
  existing notification/toast channel.
