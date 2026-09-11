# Release checklist

The single ordered path from a green `master` to a published GitHub release.
Each step names the exact script or VS Code task, so the whole flow is
copy-pasteable. Steps marked **HUMAN** need a person (credentials, a UAC
prompt, or ears/eyes on a clean machine); everything else is scripted.

Ticket: [0019](tickets/0019-release-checklist-doc.md). Related: 0015 (code
signing — not yet implemented, see step 6).

## 0. Prerequisites (once per machine)

- [ ] Windows 10/11 with **Inno Setup 6.3+**: `winget install JRSoftware.InnoSetup`.
- [ ] Node 18+ (the WebUI build).
- [ ] CMake 3.22+ and a JUCE 8.0.14 tree at `C:/JUCE/JUCE`, patched with
      `cmake/patches/juce-webview2-additional-args.patch` (see the README
      Prerequisites). The WebView2 SDK is found via
      `JUCE_WEBVIEW2_PACKAGE_LOCATION` (CI installs it from NuGet).
- [ ] A GitHub token for step 8, in `$env:GH_TOKEN` or
      `$HOME\.blueprinter-gh-token` (fine-grained `Contents: read and write`,
      or classic `repo`). **HUMAN** — the token owner creates it.

## 1. Confirm the base is green

- [ ] `master` is pushed and its **CI** run (`.github/workflows/build.yml`) is
      green: WebUI build, Release standalone + VST3, and the CTest suite.
- [ ] The working tree is clean (`git status`), or only release-version edits
      remain (step 2).

## 2. Bump the version (single source of truth)

- [ ] `CMakeLists.txt` → `project(BluePrinter VERSION X.Y.Z)`. This is what
      `build-release.ps1` and `create-release.ps1` parse; change it first.
- [ ] `installer/BluePrinter.iss` → the fallback `#define MyAppVersion "X.Y.Z"`
      (only used for manual ISCC runs; the script overrides it with `/D`).
- [ ] `WebUI/package.json` (and the matching `version` in
      `WebUI/package-lock.json`) — not read by the release scripts, but keep it
      in step.
- [ ] `README.md` — the installer filename examples in **Install**.
- [ ] `AGENTS.md` — the VS Code task line that names the installer output.
- [ ] Commit and push the version bump; wait for CI to go green.

## 3. Build the release bundle

- [ ] VS Code task **Build Release Bundle** (or `installer\build-release.ps1`).
      It: builds the WebUI (`npm run build`), builds the Release
      `BluePrinter_Standalone` + `BluePrinter_VST3`, compiles the installer
      with `/DMyAppVersion=<version>`, then assembles
      `build\release\BluePrinter-<version>\` with the installer, `LICENSE`,
      `README.md`, and `SHA256SUMS.txt`.
- [ ] The script prints the bundle contents — confirm all four files are
      present and non-zero.

## 4. Verify the bundle

- [ ] `SHA256SUMS.txt` lists every other file in the folder.
- [ ] Re-hash one file and compare:
      `Get-FileHash .\build\release\BluePrinter-<version>\BluePrinterSetup-<version>.exe -Algorithm SHA256`.
- [ ] Check the installer actually contains the current UI: the
      `WebUI\dist` it embeds was rebuilt by step 3.

## 5. Clean-machine smoke test — **HUMAN**

On a machine (or VM) that has never run BluePrinter:

- [ ] Run `BluePrinterSetup-<version>.exe` (accept the UAC prompt; SmartScreen
      will warn while the build is unsigned — see step 6).
- [ ] The **standalone** launches, the WebView2 UI loads, and the splash
      finishes.
- [ ] Add a VST3 to a chain, play/record a take, save it, and confirm it
      appears in the library.
- [ ] In a DAW, rescan VST3s and load **BluePrinter.vst3**; confirm the editor
      opens and audio passes through.
- [ ] Uninstall via Settings → Apps; confirm the program files/shortcuts are
      gone and `%APPDATA%\Retrokielto` + the snippet library survive.

## 6. Code signing — **HUMAN / BLOCKED on 0015**

Not implemented yet. Until then the installer and binaries are **unsigned** and
SmartScreen shows "Windows protected your PC" (README documents the
"More info → Run anyway" workaround). Signing must happen **before** the
bundle is assembled (step 3), so re-run step 3 after signing. See ticket 0015
for the signing-identity decision. **HUMAN** — a maintainer with the code-
signing certificate.

## 7. Draft the GitHub release

- [ ] `installer\create-release.ps1` (VS Code task **Create GitHub Release
      (draft)**). It tags `v<version>` at the default-branch head and creates a
      **draft** release with every bundle file as an asset, via the REST API.
      Pass `-Published` to skip the draft — don't, until step 8 is done.
- [ ] The script fails loudly if the tag already exists at a different commit
      or a release for that tag already exists — bump the version instead.

## 8. Review and publish — **HUMAN**

- [ ] Open the draft release link the script prints. Check the tag, the assets
      (installer + `LICENSE` + `README.md` + `SHA256SUMS.txt`), and the notes.
- [ ] Publish the release.

## Rollback

- [ ] A bad release: keep it as a **draft** (never publish) and delete it; the
      tag can be deleted and recreated at the correct commit. To fix forward,
      bump the patch version and repeat from step 2.
