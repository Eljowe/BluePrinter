#!/usr/bin/env bash
#
# Builds the unsigned macOS release bundle (ticket 0044 / ADR-0006):
#
#   build/release/BluePrinter-<version>-macos/
#     BluePrinter-<version>.dmg      (standalone .app + AU + VST3 + LICENSE + README)
#     LICENSE
#     README.md
#     SHA256SUMS.txt
#
# The build is UNSIGNED: notarization is deferred (ADR-0006). Users open the
# app via right-click -> Open (or `xattr -dr com.apple.quarantine`). Run this
# on macOS with JUCE at -DJUCE_DIR and the WebUI buildable.
#
# Version comes from CMakeLists.txt (single source of truth), like the
# Windows build-release.ps1.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${BLUEPRINTER_BUILD_DIR:-$root/build}"

cmake_lists="$(cat "$root/CMakeLists.txt")"
if [[ "$cmake_lists" =~ project\(BluePrinter\ VERSION\ ([0-9]+\.[0-9]+\.[0-9]+) ]]; then
    version="${BASH_REMATCH[1]}"
else
    echo "Could not parse version from CMakeLists.txt" >&2
    exit 1
fi
echo "Building BluePrinter $version macOS release..."

# 1. WebUI
(cd "$root/WebUI" && npm run build)

# 2. Release formats (AU is macOS-only)
cmake --build "$build_dir" --config Release --target \
    BluePrinter_Standalone BluePrinter_AU BluePrinter_VST3

art="$build_dir/BluePrinter_artefacts/Release"
app="$art/Standalone/BluePrinter.app"
au="$art/AU/BluePrinter.component"
vst3="$art/VST3/BluePrinter.vst3"

for p in "$app" "$au" "$vst3"; do
    if [ ! -e "$p" ]; then
        echo "Expected build artefact not found: $p" >&2
        exit 1
    fi
done

# 3. Stage the DMG contents
out="$root/build/release/BluePrinter-$version-macos"
stage="$out/stage"
rm -rf "$stage"
mkdir -p "$stage"

cp -R "$app"  "$stage/"
cp -R "$au"   "$stage/"
cp -R "$vst3" "$stage/"
cp "$root/LICENSE" "$root/README.md" "$stage/"

# Drag-to-Applications shortcut for the app.
ln -s /Applications "$stage/Applications"

cat > "$stage/INSTALL.txt" <<'TXT'
BluePrinter for macOS — unsigned build
======================================

This build is NOT notarized (see ADR-0006), so macOS Gatekeeper will warn the
first time you open the app.

Standalone app
--------------
Drag BluePrinter.app to Applications (or copy it anywhere).

Plugins
-------
Copy the plugin bundles into your user plug-in folders (create them if they
do not exist):

  BluePrinter.component  ->  ~/Library/Audio/Plug-Ins/Components/
  BluePrinter.vst3       ->  ~/Library/Audio/Plug-Ins/VST3/

or, for all users (needs admin):

  /Library/Audio/Plug-Ins/Components/
  /Library/Audio/Plug-Ins/VST3/

First launch
------------
Right-click BluePrinter.app -> Open, then confirm "Open" (or run
  xattr -dr com.apple.quarantine /Applications/BluePrinter.app
). Audio Units may need a one-time validation by your DAW on first scan.
TXT

# 4. Build the (unsigned) DMG
rm -f "$out/BluePrinter-$version.dmg"
hdiutil create \
    -volname "BluePrinter $version" \
    -srcfolder "$stage" \
    -ov -format UDZO \
    "$out/BluePrinter-$version.dmg"

rm -rf "$stage"

# 5. Checksums + top-level convenience copies
cp "$root/LICENSE" "$root/README.md" "$out/"
(
    cd "$out"
    shasum -a 256 "BluePrinter-$version.dmg" > SHA256SUMS.txt
)

echo ""
echo "Release bundle: $out"
ls -lh "$out" | awk 'NR > 1 { printf "  %s  (%s)\n", $9, $5 }'
