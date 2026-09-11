# Chains run in parallel and never hear each other

VST3 chains are independent parallel paths, not a serial rack. Each chain copies
its selected input channels from a pristine, pre-loop `chainInputBuffer` snapshot
into its own scratch buffer and processes there; no chain ever reads the
accumulating output mix. This lets a synth chain and a guitar chain coexist
without either processing the other's signal, and a chain with no active
(non-bypassed) plugins is skipped entirely so a transparent chain cannot double
the dry signal.

## Consequences

- Chain order in the UI is presentational only — it does not imply signal flow.
- MIDI is likewise copied per chain (`chainMidiScratch`) and filtered by channel;
  there is no cross-chain MIDI flow.
- The recording mix is the sum of the dry input and each Record-enabled chain's
  output, so "all chains on" matches the old single-chain tap.
