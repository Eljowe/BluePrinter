---
id: "0044"
title: "macOS packaging (unsigned)"
status: ready-for-agent
blocked_by: ["0042"]
---

# macOS packaging (unsigned)

## Problem

The release pipeline is Windows-only (Inno Setup + PowerShell). macOS has no
installable artefact. Per
[ADR-0006](../adr/0006-cross-platform-macos-linux.md) v1 ships **unsigned**;
notarization is deferred.

## Required change

1. Produce a macOS bundle: Standalone `.app` plus the AU/VST3 plugins, packaged
   as a `.dmg` (and/or `.pkg`) with the usual `~/Library/Audio/Plug-Ins/
   {Components,VST3}` / `/Library/...` install locations, plus the standalone app
   in `/Applications` or dragged to Applications.
2. A portable packaging script alongside the Windows release scripts; keep the
   Windows path untouched.
3. Document that the build is **unsigned**: users open it via right-click→Open
   (or `xattr -dr com.apple.quarantine`) and that Audio Units are validated only
   after first launch. No installer step may assume notarization.
4. Wire the artefact into the release bundle/asset flow (ticket
   [0019](0019-release-checklist-doc.md) / the create-release script) without
   breaking the Windows bundle.

## Acceptance criteria

- [ ] The packaging script produces a `.dmg` containing the standalone `.app`,
      the AU and the VST3.
- [ ] Installing the AU and VST3 into the standard user/system plugin folders
      makes them visible to a macOS DAW (manual smoke test).
- [ ] The standalone launches; the UI loads in WKWebView; the bridge works.
- [ ] The docs state the build is unsigned and how to open it.
- [ ] The Windows release bundle is unchanged.

## Docs

`README.md` (macOS install), `docs/release-checklist.md`, `AGENTS.md`
(release/packaging), `docs/adr/0006-cross-platform-macos-linux.md`.

## Files

`installer/` (new macOS script), release workflow, `README.md`, `CMakeLists.txt`
(bundle id / plist metadata if missing).

## Out of scope

Notarization/signing (a follow-up once an Apple Developer account exists);
Mac App Store distribution.
