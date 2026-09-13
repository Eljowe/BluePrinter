---
id: "0045"
title: "Linux packaging (AppImage + .deb)"
status: ready-for-agent
blocked_by: ["0042"]
---

# Linux packaging (AppImage + .deb)

## Problem

There is no Linux installable artefact. The UI needs the WebKitGTK runtime,
which must be declared and documented.

## Required change

1. Produce a **`.deb`** that installs the standalone, the VST3 bundle to the
   standard VST3 path (`/usr/lib/vst3` or `~/.vst3`), a `.desktop` entry and an
   icon, and declares `libwebkit2gtk-4.1-0` as a runtime dependency.
2. Produce an **AppImage** of the standalone (with the VST3 available alongside
   or documented as a separate install) for distros without a package manager
   path.
3. A Linux packaging script alongside the Windows/macOS ones; wire the artefacts
   into the release bundle/asset flow without breaking the others.
4. README install section: the `webkit2gtk-4.1` dependency and how to install
   the plugin into `~/.vst3` / `/usr/lib/vst3`, plus the note that third-party
   VST3 amp-sim availability on Linux is limited.

## Acceptance criteria

- [ ] `dpkg -i` installs the app, `.desktop` entry and VST3; launching the
      standalone loads the UI in WebKitGTK and the bridge works (manual smoke
      test on Ubuntu 22.04+).
- [ ] The `.deb` declares the WebKitGTK 4.1 dependency.
- [ ] The AppImage runs the standalone on Ubuntu 22.04+ with `webkit2gtk-4.1`
      present.
- [ ] The other platforms' release bundles are unchanged.

## Docs

`README.md` (Linux install + runtime dependency), `docs/release-checklist.md`,
`AGENTS.md`, `docs/adr/0006-cross-platform-macos-linux.md`.

## Files

`installer/` (new Linux script + `.desktop`/icon), release workflow, `README.md`.

## Out of scope

Flatpak/Snap; distro-specific repos; signing.
