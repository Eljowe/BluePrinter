---
id: "0043"
title: "Portable crash diagnostics (macOS/Linux)"
status: done
blocked_by: ["0042"]
---

# Portable crash diagnostics (macOS/Linux)

## Problem

Crash self-heal reads `%APPDATA%\Retrokielto\crash-info.txt`, written by a
Windows-only `SetUnhandledExceptionFilter` handler (WER LocalDumps + rotation).
On macOS/Linux there is no equivalent, so the restore self-heal and the in-app
diagnostics report degrade to "no crash info".

## Required change

1. Add a POSIX crash handler (`sigaction` for SIGSEGV/SIGABRT/SIGILL/SIGFPE/
   SIGBUS) on macOS/Linux that writes the same `crash-info.txt` format the
   existing parser reads: `Operation:` line (via `setCrashOp`/`getCrashOp`), a
   backtrace (`backtrace`/`backtrace_symbols` where available), and the
   timestamp.
2. Rotate to `crash-info.1/.2/.3.txt` on each new crash, mirroring the Windows
   handler (degrade to truncate/overwrite where atomic rename is unavailable).
3. Keep `RestoreSelfHeal` parsing endpoint-compatible: the same file, folder and
   "Operation:" line, with the same freshness gating (the launch-time properties
   mtime fallback already exists for hosts without a settings file).
4. Degrade gracefully when a backtrace cannot be produced (still write the op +
   header). Never throw from the handler.
5. `buildDiagnosticsReport` continues to work; on macOS/Linux it includes the
   rotated `crash-info*.txt` files it finds. The WER/LocalDumps command snippet
   is Windows-only and must be omitted elsewhere.

## Acceptance criteria

- [ ] A deliberate crash on macOS and Linux writes a `crash-info.txt` with the
      current operation and a backtrace (where available).
- [ ] Rotation keeps the previous crash's file.
- [ ] `RestoreSelfHeal::plan` quarantines the same way it does on Windows when
      the file names a plugin.
- [ ] The diagnostics report builds on all platforms; the WER snippet is omitted
      off-Windows.
- [ ] The Windows handler and its tests are unchanged.

## Docs

`AGENTS.md` (crash diagnostics section), `docs/adr/0006-cross-platform-macos-linux.md`.

## Files

`Source/PluginProcessor.cpp` (handler + `setCrashOp`), `Source/RestoreSelfHeal.{h,cpp}`,
`Tests/test_RestoreSelfHeal.cpp`.

## Out of scope

Coredumps/Mach exception ports; uploading crash reports.

## Comments

2026-09-13 — Implemented:

- `PluginProcessor.cpp`: a `#if ! JUCE_WINDOWS` block installs POSIX
  `sigaction` handlers (SIGSEGV/ABRT/ILL/FPE/BUS) from the processor
  constructor. The handler writes `crash-info.txt` (same folder via
  `userApplicationDataDirectory/Retrokielto`, same `Operation:` line from
  `getCrashOp()`), adds `Signal:`/`Time (unix seconds):` and a
  `backtrace()` address list, rotates `.1/.2/.3` with `rename()`, then
  re-raises with the default disposition. Async-signal-safe only: fixed
  buffers, hand-rolled number formatting, `open`/`write`/`close`/`rename`;
  the folder is resolved once on the message thread. No `<execinfo.h>` →
  header/op still written (backtrace omitted).
- `buildDiagnosticsReport`: the WER LocalDumps snippet is `#if JUCE_WINDOWS`;
  macOS/Linux print the `ulimit -c`/symbolication note instead.
- `Tests/test_RestoreSelfHeal.cpp`: `RestoreSelfHeal_parsesThePosixCrashFormat`
  confirms the parser ignores the extra Signal/Time/Backtrace lines and still
  names the plugin.
- `AGENTS.md` crash-diagnostics paragraph updated.

Verified locally on Windows: Debug standalone + tests build, `ctest` 2/2
green. The POSIX block is not compiled on Windows, so it was additionally
syntax-checked with WSL g++ (extracted block + minimal JUCE stubs, `-Wall
-Wextra`) and run to confirm it writes the file and rotates .1/.2/.3 before
re-raising. macOS/Linux compile+tests are verified by CI (run 34779042885 — all three
jobs green).

Acceptance criterion 1 (a deliberate crash on macOS/Linux writes the file)
is not exercised by CI; the delivered-crash smoke test is a human step.
