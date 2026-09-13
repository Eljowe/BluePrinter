#!/usr/bin/env bash
#
# Builds the Linux release artefacts (ticket 0045 / ADR-0006):
#
#   build/release/BluePrinter-<version>-linux/
#     BluePrinter-<version>.deb                  (standalone + VST3 + .desktop)
#     BluePrinter-<version>-x86_64.AppImage      (portable standalone)
#     LICENSE
#     README.md
#     SHA256SUMS.txt
#
# The standalone and VST3 depend on WebKitGTK 4.1 at runtime; the .deb
# declares it. Run on Ubuntu 22.04+ with the JUCE Linux build deps (see the
# CI workflow) and a WebUI that builds. Version comes from CMakeLists.txt.
#
# AppImage creation needs `appimagetool` (downloaded if absent) and an icon
# converter (`rsvg-convert` or ImageMagick `convert`); it is skipped with a
# warning if unavailable.

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
echo "Building BluePrinter $version Linux release..."

arch="$(dpkg --print-architecture 2>/dev/null || echo amd64)"

# 1. WebUI
(cd "$root/WebUI" && npm run build)

# 2. Release formats
cmake --build "$build_dir" --config Release --target BluePrinter_Standalone BluePrinter_VST3

art="$build_dir/BluePrinter_artefacts/Release"
standalone_bin="$art/Standalone/BluePrinter"
standalone_webui="$art/Standalone/WebUI"
vst3="$art/VST3/BluePrinter.vst3"

for p in "$standalone_bin" "$standalone_webui" "$vst3"; do
    if [ ! -e "$p" ]; then
        echo "Expected build artefact not found: $p" >&2
        exit 1
    fi
done

out="$root/build/release/BluePrinter-$version-linux"
rm -rf "$out"
mkdir -p "$out"

desktop_entry() {
    cat <<'EOF'
[Desktop Entry]
Type=Application
Name=BluePrinter
GenericName=Guitar take recorder
Comment=Record and organise guitar takes, hosting VST3 chains
Exec=blueprinter
Icon=blueprinter
Terminal=false
Categories=AudioVideo;Audio;Music;
Keywords=guitar;audio;recorder;vst3;
EOF
}

# ============================================================================
# .deb
# ============================================================================
debroot="$out/debroot"
mkdir -p "$debroot/DEBIAN"
mkdir -p "$debroot/opt/blueprinter"
mkdir -p "$debroot/usr/bin"
mkdir -p "$debroot/usr/lib/vst3"
mkdir -p "$debroot/usr/share/applications"
mkdir -p "$debroot/usr/share/icons/hicolor/scalable/apps"

install -m 0755 "$standalone_bin" "$debroot/opt/blueprinter/BluePrinter"
cp -R "$standalone_webui" "$debroot/opt/blueprinter/WebUI"
ln -s /opt/blueprinter/BluePrinter "$debroot/usr/bin/blueprinter"
cp -R "$vst3" "$debroot/usr/lib/vst3/"
if [ -f "$root/Source/icon.svg" ]; then
    cp "$root/Source/icon.svg" "$debroot/usr/share/icons/hicolor/scalable/apps/blueprinter.svg"
fi
desktop_entry > "$debroot/usr/share/applications/blueprinter.desktop"

installed_size="$(du -sk "$debroot" | cut -f1)"
cat > "$debroot/DEBIAN/control" <<EOF
Package: blueprinter
Version: $version
Section: sound
Priority: optional
Architecture: $arch
Depends: libwebkit2gtk-4.1-0 | libwebkit2gtk-4.0-37, libasound2 (>= 1.0.17)
Installed-Size: $installed_size
Maintainer: Retrokielto <noreply@example.com>
Description: Guitar take recorder with a VST3 FX chain and looper
 BluePrinter records guitar takes through a chain of hosted VST3 plugins,
 with a looper, a built-in tuner and a snippet library. This package ships
 the standalone app and the VST3 plugin. The UI needs the WebKitGTK 4.1
 runtime, declared as a dependency.
EOF

dpkg-deb --build --root-owner-group "$debroot" "$out/BluePrinter-$version.deb" >/dev/null
rm -rf "$debroot"

# ============================================================================
# AppImage (best-effort: needs appimagetool + an icon converter)
# ============================================================================
appid_appdir="$out/AppDir"
converter=""
if command -v rsvg-convert >/dev/null 2>&1; then
    converter="rsvg-convert"
elif command -v convert >/dev/null 2>&1; then
    converter="convert"
fi

appimagetool=""
if command -v appimagetool >/dev/null 2>&1; then
    appimagetool="appimagetool"
else
    tmp_tool="$out/.tools"
    mkdir -p "$tmp_tool"
    url="https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage"
    if curl -fsSL -o "$tmp_tool/appimagetool" "$url" 2>/dev/null; then
        chmod +x "$tmp_tool/appimagetool"
        appimagetool="$tmp_tool/appimagetool"
    fi
fi

if [ -n "$appimagetool" ] && [ -n "$converter" ] && [ -f "$root/Source/icon.svg" ]; then
    rm -rf "$appid_appdir"
    mkdir -p "$appid_appdir/usr/bin"
    cat > "$appid_appdir/AppRun" <<'EOF'
#!/bin/sh
here="$(dirname "$(readlink -f "$0")")"
exec "$here/usr/bin/BluePrinter" "$@"
EOF
    chmod +x "$appid_appdir/AppRun"
    desktop_entry > "$appid_appdir/blueprinter.desktop"
    cp "$root/Source/icon.svg" "$appid_appdir/blueprinter.svg"
    if [ "$converter" = "rsvg-convert" ]; then
        rsvg-convert -w 256 -h 256 "$root/Source/icon.svg" -o "$appid_appdir/blueprinter.png"
    else
        convert -background none -resize 256x256 "$root/Source/icon.svg" "$appid_appdir/blueprinter.png"
    fi
    install -m 0755 "$standalone_bin" "$appid_appdir/usr/bin/BluePrinter"
    cp -R "$standalone_webui" "$appid_appdir/usr/bin/WebUI"

    # appimagetool is itself an AppImage; extract-and-run avoids FUSE in CI.
    if ARCH="$arch" APPIMAGE_EXTRACT_AND_RUN=1 \
        "$appimagetool" "$appid_appdir" "$out/BluePrinter-$version-x86_64.AppImage" >/dev/null 2>&1; then
        rm -rf "$appid_appdir"
        rm -rf "$out/.tools"
    else
        echo "warning: appimagetool failed; AppImage not produced" >&2
    fi
else
    echo "warning: appimagetool/icon converter unavailable; AppImage not produced" >&2
fi

# ============================================================================
# Bundle top level
# ============================================================================
cp "$root/LICENSE" "$root/README.md" "$out/"

(
    cd "$out"
    rm -f SHA256SUMS.txt
    for f in BluePrinter-*.deb BluePrinter-*.AppImage; do
        [ -e "$f" ] && shasum -a 256 "$f" >> SHA256SUMS.txt
    done
)

echo ""
echo "Release bundle: $out"
ls -lh "$out" | awk 'NR > 1 { printf "  %s  (%s)\n", $9, $5 }'
