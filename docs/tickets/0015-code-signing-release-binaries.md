---
id: "0015"
title: "Code-sign the installer and binaries"
status: ready-for-human
blocked_by: []
---

# Code-sign the installer and binaries

## Problem

The installer and binaries are unsigned, so Windows SmartScreen shows "Windows
protected your PC" and the download looks untrustworthy. `README.md` currently
tells users to click "More info → Run anyway", which is a poor first impression for
a consumer release and blocks wider adoption.

## Current behaviour

`installer/build-release.ps1` builds and compiles the installer
(`installer/BluePrinter.iss`) and assembles a release bundle with
`SHA256SUMS.txt`, but nothing is signed.

## Required change

1. Obtain a signing identity — recommended **Azure Trusted Signing** (lowest
   recurring cost, identity-validated, has a GitHub Action); alternative is an
   OV/EV certificate. This step needs the maintainer's identity/billing and has
   external lead time, so start it early.
2. Sign the standalone `.exe`, the `BluePrinter.vst3` bundle, and the installer.
3. Sign **before** hashing so `SHA256SUMS.txt` matches the shipped bytes.
4. Document the signing setup and any CI secrets/identities required.

## Acceptance criteria

- A downloaded installer runs without a SmartScreen "unknown publisher" wall.
- `signtool verify /pa` succeeds on the installer and the VST3.
- `build-release.ps1` signs before computing `SHA256SUMS.txt`.
- Setup is documented (who owns the identity, how to sign, where the secrets live).

## Docs

`README.md` (remove the "Run anyway" instruction once signed),
`docs/release-checklist.md` (0019), `AGENTS.md` (VS Code tasks).

## Files

`installer/build-release.ps1`, `installer/BluePrinter.iss`,
`.github/workflows/build.yml` (if signing moves to CI).

## Out of scope

Auto-update infrastructure; Microsoft Store packaging.
