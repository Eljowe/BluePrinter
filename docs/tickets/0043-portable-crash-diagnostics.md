---
id: "0043"
title: "Portable crash diagnostics (macOS/Linux)"
status: ready-for-agent
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
