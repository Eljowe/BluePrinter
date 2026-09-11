# Gate persist while a chain restore is in progress

Persistence is suppressed while a deferred chain restore is running, and
`getStateInformation` omits the `pluginChains` property entirely (also for the
whole launch after a self-heal) until the restore completes. During restore the
in-memory chain list is partial or empty; an unguarded save would overwrite the
good on-disk state with that partial state — which wiped a user's saved chains
once, and, via the JUCE standalone's own settings file, could propagate an empty
or default plugin state to the next launch ("states lost on close"). Skipping the
write makes the next launch fall back to the properties file, which still holds
the last good state.

## Consequences

- `persistPluginChain` does not arm and `flushPendingChainPersist` drops stale
  arms while `isChainRestoreInProgress()` is true.
- The crash marker is honoured only when no clean exit postdates it, so a plain
  quit mid-restore does not make the next launch load plugins with defaults.
- The first user mutation after the restore completes persists normally.
