# Chains: state, deferred restore and crash self-heal

Moved verbatim from `AGENTS.md` (ticket 0064, 2026-11-21). Rules stay in
[`AGENTS.md`](../../AGENTS.md); this file keeps the mechanics and incident history. The
ownership/lifetime and persist-gating rules are stated in AGENTS.md; this doc holds the
serialisation formats, the restore driver and the crash history.

## Serialised chain state and migration

- **Persistence**: `makeChainState` emits `{ chains: [...], nextChainId, blocklist, availablePlugins }`;
  `applyChainState` migrates the old `midiChain`/`audioChain` split format (→ two chains "MIDI
  Chain"/"Audio FX Chain") and the pre-split single `slots` format (→ one chain).
  `ensureUniqueChainIds` fixes missing/duplicate ids after restore. Absent
  `volume`/`muted`/`midiChannels` in old states default to 0 dB / unmuted / all channels.
- `loadSavedChainState()` returns the `pluginChains` key whenever it parses to a valid bundle object
  (even with empty chains — the user removed the plugins); the legacy `pluginChain` key only serves
  as a fallback for states that never saw the new format (it can contain same-chain duplicates).

## Parameter changes inside hosted plugins

The persist rules that these listeners feed are in AGENTS.md ("VST3 Chains"); the listener mechanics
are:

- **Parameter tweaks inside hosted plugins persist too**: every `PluginChain` implements
  `juce::AudioProcessorListener` (attached to each plugin instance on load — `addPlugin` and
  `finalizeAsyncLoad` — and removed before destruction in `removePlugin`/`clear`), and forwards
  `audioProcessorParameterChanged`/`audioProcessorChanged` to `onChanged`, which arms the same
  debounced persist (it never pushes UI snapshots, so drags/automation don't spam the frontend).
  **It ALSO implements `juce::AudioProcessorParameter::Listener`** and attaches to every parameter
  of each hosted plugin (`attachParameterListeners`/`detachParameterListeners`, re-bound from
  `audioProcessorChanged` when `parameterInfoChanged` is set). This is required because the
  `AudioProcessorListener` callbacks do NOT fire for ordinary parameter edits by modern plugins —
  JUCE's `AudioProcessor::ParameterChangeForwarder` is registered only by `LegacyAudioParameter`, so
  an APVTS-based plugin that just calls `setValueNotifyingHost` (rather than
  `updateHostDisplay`/`setDirty`) never armed the persist and its state was silently never re-saved
  (the "Obstacle" bug: its blob in `BluePrinter.properties` stayed byte-identical for weeks while
  notification-capable plugins' blobs changed). Those callbacks can fire from a plugin's audio
  thread, so `persistPluginChain` and its flags (`persistingPluginChain`, `chainPersistPending`,
  `chainPersistDeadline`) are atomics callable from any thread; the flush still runs on the message
  thread (timer/destructor).

## Deferred chain restore: one plugin per message-loop turn

- **Deferred chain restore**: `setChainState` never instantiates plugins — saved slots are queued as
  `PendingSlot`s (path, bypass, state blob) and the processor's `timerCallback` (the
  `pendingPluginLoads`/`restoreActive` driver) loads them ONE per message-loop turn via
  `addPluginAsync` (30 s timeout per slot — heavy amp sims are slow on cold start), applying bypass
  in the load callback and the saved state blob on the NEXT loop turn (`pendingStateApply` — queued
  by the callback, applied by `timerCallback` one turn later; the next slot is popped and its load
  started FIRST so the state poke overlaps that load on the apartment thread — `createInstance` runs
  on the apartment while `setStateInformation` blocks the main thread — leaving only the LAST slot's
  poke exposed as the splash tail; the state blob applies to bypassed slots too — bypass is
  BluePrinter's own routing flag, and skipping the poke made every bypassed slot come back in
  DEFAULTS every launch, which then got persisted over the good blobs on the first mutation). This
  is deliberate: synchronously instantiating several plugins in a row kept the message thread inside
  plugin code for hundreds of ms, so window messages plugins queue during their own setup got
  dispatched reentrantly and crashed some plugins (Neural DSP "X" amp sims died with a heap fault in
  the first instance's window proc). One slot per loop turn gives every plugin an idle gap (the
  apartment serializes the factory calls FIFO; the main loop never runs plugin window code), and the
  extra turn before the state poke lets its queued window messages be dispatched by the normal pump
  first — applying `setStateInformation` in the same turn as the finalize made some plugins run
  their window proc reentrantly from inside their own code and die with a heap fault.
- **All VST3 factories run on the plugin UI apartment**
  (`PluginUiApartment`/`getPluginUiApartment()`, a process-lifetime `juce::Thread` with a Win32
  `GetMessage`/`DispatchMessage` pump + a FIFO task queue; `post()` is the only entry,
  non-blocking): chain loads (`PluginChain::addPluginAsync`) AND the folder scanner
  (`Vst3Library::describeVst3FileAsync`) post their `createPluginInstance`/`findAllTypesForFile`
  calls there. Every plugin window therefore belongs to that one pumped thread — the app's main
  message loop can never dispatch into plugin window code. That was the crash path for the SECOND
  Neural DSP "X" instance (a −1-pointer AV in its own WndProc while its factory was still running on
  a pump-less detached worker; windows created on a pump-less thread get their messages dispatched
  reentrantly by whoever pumps — us). Timeout-abandoned loads now also catch the broken-promise
  `set_value` (the old detached-worker path terminated on `std::future_error`).
- **The driver copies `slot.file` to a local BEFORE the `slot = std::move (slot)` capture-init** —
  MSVC evaluates the lambda capture before earlier function arguments, so passing `slot.file`
  directly handed the loader a moved-from (empty) path and the restore silently loaded nothing.
  `createChain` wires `onChanged` → `persistPluginChain` on every chain (defaults, UI-created, and
  restored), so slot mutations on restored chains persist too. The UI shows per-chain `pending`
  counts + a `restoring` flag in the `backendVst3Chain` snapshot so a slow restore looks like
  progress, not a hang.
- **Eager `prepareToPlay` at load is gated on `BluePrinterAudioProcessor::isDevicePrepared()`** (set
  by the host's own `prepareToPlay`): before the device/DAW prepare, the chain's
  `currentSampleRate`/`currentBlockSize` still hold the 44100/512 defaults, and a fake-then-real
  double prepare with a block-size change (DSP teardown + rebuild) is one of the things that makes
  Neural DSP "X" amp sims die at load with a heap fault / stack-cookie fail-fast — so restore-time
  and manual-add loads skip the eager prepare until the real config is known; the host's
  `prepareToPlay` covers every chain, and anything loaded after the device started prepares eagerly
  with real values. **Same-chain duplicates of the same .vst3 are rejected** (`hasPluginFile`)
  because two instances in one chain crash those plugins; the same plugin on *different* chains is
  fine.

## Crash marker and self-heal

- A `chainRestoreCrashed` properties-file marker self-heals: if a launch crashes mid-restore, the
  next launch skips state-blob restore (plugins load with defaults). The marker is set at the start
  of `applyChainState` and cleared only once the deferred restore has fully drained
  (`restoreRequestedThisSession` gates the timer-side clear so a plain tick can't wipe a stale
  marker) — clearing it at the end of `applyChainState`, before the deferred loads even started,
  made the same crashing blob apply on every launch (crash, clear-marker, crash again — the
  Archetype Tim Henson X heap fault). **The marker also gets left set by a plain (non-crash) quit
  mid-restore** — it is written at every restore start and drains don't run when the app is gone —
  and trusting it next launch loaded every plugin with DEFAULTS ("states lost on close" again). So
  the marker is honored only if NO clean exit postdates it: `chainRestoreMarkerTime` (epoch millis,
  string property) is stored next to it, and `BluePrinter.settings` mtime (written exclusively by
  clean exits, `closeButtonPressed` → `savePluginState`) newer than it marks the marker stale
  (`chainRestoreBlobsAllowed`). The staleness decision is made once per process
  (`chainRestoreDecisionMade`) because the standalone runs `applyChainState` twice at launch
  (settings-file restore in `setStateInformation`, then the editor's `restoreSavedPluginChains`);
  DAW hosts have no settings file and keep honoring the marker.

## Crash diagnostics: crash-info, map files and WER dumps

- Crash diagnostics: on Windows a `SetUnhandledExceptionFilter` writes
  `%APPDATA%\Retrokielto\crash-info.txt` (current restore op + module-offset backtrace); on
  macOS/Linux a POSIX `sigaction` handler (SIGSEGV/ABRT/ILL/FPE/BUS, installed at processor
  construction) writes the same file with the same `Operation:` line, a `Signal:`/`Time:` line and a
  `backtrace()` address list, then re-raises so the OS still records its own dump (ticket 0043).
  Both paths use `setCrashOp`/`getCrashOp` and stay async-signal-safe (fixed buffers, no heap/locks;
  the folder is resolved on the message thread at startup); the file **rotates** to
  `crash-info.1/.2/.3.txt` on each new crash (kernel32 `MoveFileExA` / POSIX `rename()` in the
  handler) so the last crash isn't lost, and the **in-app Diagnostics** popover (0016, footer →
  `frontendCopyDiagnostics`/`frontendOpenDiagnosticsFolder`) copies an audio-free support bundle
  (`buildDiagnosticsReport`: version/OS/CPU, settings counts, quarantine, restore error, all rotated
  crash files + the WER LocalDumps command) to the clipboard or opens `%APPDATA%\Retrokielto`.
  **Release builds emit a PDB *and* a linker `/MAP` file** (`/Zi` `/DEBUG`,
  `/MAP:...BluePrinter.map` — `CMakeLists.txt`); the map is the primary offset→symbol path because
  dbghelp `SymFromAddr` proved unreliable in this environment (scripts/symbolize-crash.ps1) and the
  registered msdia version can't parse these PDBs (scripts/pdbmap.cpp needs the matching DIA SDK,
  registered). Use `scripts/map-crash.ps1 -Map build\BluePrinter_artefacts\Release\Standalone\BluePrinter.map -CrashInfo "%APPDATA%\Retrokielto\crash-info.txt"`
  — the map only lists public/code symbols, so a fault can land in an inlined/unlisted thunk near a
  listed symbol (nearest-symbol output can misattribute: verify with `dumpbin /disasm`). Offsets
  shift when the binary is relinked, so map a `crash-info.txt` only against the exe/map built at the
  same time. For symbolatable stacks, enable WER LocalDumps for BluePrinter.exe (elevated: `reg add "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\BluePrinter.exe" /v DumpFolder /t REG_EXPAND_SZ /d C:\BluePrinterDumps /v DumpType /t REG_DWORD /d 2 /v DumpCount /t REG_DWORD /d 10 /f`)
  and open the dump in WinDbg against the build's PDBs (plugin frames come back as module+offset;
  Neural DSP ships no public PDBs).

## Plugin quarantine

- **Plugin quarantine** extends the self-heal: the state-blob skip alone can't defuse crashes that
  happen at `createPluginInstance`/`finalizeAsyncLoad` (*before* any state is applied — the Neural
  DSP "X" heap faults are exactly this). So `applyChainState` parses `crash-info.txt`'s "Operation:"
  line on EVERY launch (not just when the marker was set — a crash outside a deferred restore, e.g.
  a manual plugin add, leaves the marker clear, and those crashes need the quarantine just as much):
  freshness-gated against the LAST CLEAN EXIT (`BluePrinter.settings` mtime) — a crashed session
  writes the chainRestoreCrashed marker into the properties AFTER its own crash diagnostics, so the
  old anchor (properties mtime at launch) made the very crash we just suffered look stale, while the
  settings file is only ever written by clean exits, so any crash-info newer than it belongs to the
  session that just died. In DAW hosts (no settings file) it falls back to `userStateMtimeAtLaunch`
  (properties mtime captured at `restoreUserState` entry before this session wrote anything). The
  restore driver quarantines the plugin it names — matched by file name (instantiate/prepare
  crashes) or saved plugin name (setStateInformation crashes) — adding it to the persisted
  `pluginQuarantine` property (JSON array of file names).
- **Fail-fast crashes (0xC0000409, `__security_check_cookie`/BEX — the Neural DSP "X" stack-cookie
  deaths: visible in the Windows event log, but they bypass `SetUnhandledExceptionFilter`, so
  `crash-info.txt` is never updated) are covered by the `lastPluginLoadOp` property**:
  `notifyPluginLoadStarting` (message thread; called by both the restore driver before each
  `addPluginAsync` pop and by `addVst3FromPath` before a manual load) writes `<epochMillis>:<file name>`
  into the properties file, `notifyPluginLoadFinished` clears it on load completion / restore drain,
  and `readFreshLoadOpDetail` feeds it into the same quarantine match under the same freshness gate
  — a crash mid-load leaves the name on disk even when the exception filter never ran. Timed-out
  loads are NOT cleared (their abandoned worker may still be creating the instance, and a crash from
  it must stay nameable); a real load failure (worker returned) is cleared. Quarantined slots are
  dropped by the driver without ever being instantiated (`recordQuarantinedSkip` folds them into
  `lastChainRestoreError` with the remedy: "Re-add it from a chain's plugin list to try again").
- **A manual add clears the entry** (`clearPluginQuarantineForFile` in `addVst3FromPath`, before the
  load) so the user's explicit retry gets a fresh chance; if it crashes again, the plugin was never
  persisted so the next restore quarantines it anew. The `crashOpBuffer` op is reset to "no chain
  operation in progress" when the restore drains, so a crash later in the session (device start,
  plugin audio) is never misattributed to a restore slot. Each individual load op
  (`findAllTypesForFile`, `createPluginInstance`, and both `prepareToPlay`/`finalizeAsyncLoad`
  prepares) also clears the op when it returns, so a manual add or a completed scan can't leave a
  stale op that misattributes a later crash (a stale "scanning VST3 descriptions …" op once blamed a
  plugin's audio-thread crash on a scan); the take-melody analysis worker sets its own op
  (`analyzing take melody (MelodyAnalyzer)`) while it runs.
- Note the first crash of any given plugin still takes the app down once (it can't be quarantined
  before it has crashed); the quarantine kills the repeat-brand loop. **Known-good user state was
  backed up to `%APPDATA%\Retrokielto\BluePrinter.properties.fullstate`; a partial-state persist
  wiped `pluginChains` once (see the persist guard above), so if the chains come back empty, restore
  that file and strip the old `pluginChain` key. Backups from the Sep 2 2026 X-amp/Play crash
  session: `BluePrinter.properties.bak-corywong-*`, `.bak-timhenson-*`, `.real-*`, and the
  quarantine entries (`Play.vst3`, `Archetype Tim Henson X.vst3`, `Archetype Cory Wong X.vst3`) were
  left in place so those plugins are skipped until re-added manually.**

## Named chain presets (0033)

- **Named chain presets** (0033): a per-chain rig (plugin paths/order/bypass/state blobs +
  volume/mute + MIDI toggle/channels) saved one JSON file per preset under
  `%APPDATA%\Retrokielto\chain-presets`
  (`ChainPreset::{makeDocument,validateDocument,getPayload,sanitisePresetFileName}`, unit-tested; a
  `version` mismatch is refused, unknown fields warn). `PluginChain::applyPresetState` replaces a
  chain's slots (never its id/name/input mask/record flag) and queues `PendingSlot`s via the shared
  `queueSlotsFromState` helper, so loads ride the deferred restore driver;
  `PluginProcessor::loadChainPreset` pre-filters blocklisted/missing/quarantined/duplicate slots and
  reports them, mirrors the restore crash-marker bookkeeping, and pauses the driver
  (`restoreActive`) around the synchronous clear so it can't echo a momentarily-empty chain. Save is
  refused while the chain has pending slots; overwrite/rename collisions return `outError == "exists"`
  for the UI to confirm.
- Events
  `frontendSaveChainPreset`/`frontendLoadChainPreset`/`frontendRenameChainPreset`/`frontendDeleteChainPreset`;
  the chain snapshot carries `chainPresets: [{name,file}]`. The UI (`PluginChain.jsx`
  `ChainPresetMenu`) uses inline prompts/confirmations because WebView2 shows no native dialog.

## Plugin manager (0040)

- **Plugin manager** (0040): a footer **Plugins** popover (`PluginManager.jsx`) lists the cached
  `availablePlugins` (name/vendor/path) with a search filter and a rescan button (reuses
  `frontendScanVst3Folder` / `backendVst3ScanProgress`), and marks/clears quarantined files.
  `PluginProcessor::getPluginQuarantineSnapshot()` ships `quarantine: [fileName, …]` in the chain
  snapshot; `frontendClearPluginQuarantine { file }` calls `clearPluginQuarantineForFile` + persist.
  Quarantine entries with no scanned file (uninstalled or moved) are still listed so they can be
  cleared. Metadata-only — nothing here instantiates a plugin.

## Editor plugin windows

- Editor plugin windows: `editorWindows` keyed by chain id → slot index;
  `refreshChainEditorBindings()` rewires per-chain `onSlotRemoved` after every chain-list change. A
  removed chain's windows are closed exactly once at removal time by the owner's retirement
  (`retireChain` fires `onSlotRemoved` per slot; `ChainStatePersistence::clearChains` only hands the
  chain over), even though the chain object itself lives on until two snapshot refills — so a window
  can never outlive its plugin (and a restore that reuses the chain id can't inherit it).
