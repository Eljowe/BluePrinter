# Restore chain plugins one per message-loop turn

Saved VST3 chains are restored **asynchronously**, one plugin per message-loop
turn, never by instantiating several synchronously. Instantiating several heavy
plugins in a row kept the message thread inside plugin code for hundreds of
milliseconds; window messages the plugins queued during their own setup then got
dispatched reentrantly and crashed them (Neural DSP "X" amp sims died with a heap
fault inside the first instance's window proc). The state blob is also applied one
turn *after* the load finalises, so any queued window messages are dispatched by
the normal pump before `setStateInformation` runs. Loads are given a 30 s timeout
because cold-start amp sims are slow.

## Consequences

- Startup shows a splash with real per-chain progress until the restore drains.
- A plugin that keeps crashing a restore is **quarantined** (state-blob restore
  skipped and/or the file excluded) via a self-heal marker and crash diagnostics,
  so the same crash does not repeat every launch.
- Eager `prepareToPlay` at load is skipped until the host's real device config is
  known, because a fake-then-real double prepare kills some amp sims.
- All VST3 factories run on a single pumped "plugin UI apartment" thread so plugin
  windows always belong to one message pump.
