#!/usr/bin/env bash
#
# Wraps a bundle made by make-bundle.sh into a Debian package that installs the application
# under /opt, exposes it in /usr/bin and the application menu, and declares the system libraries
# it needs as dependencies.
#
# Usage: make-deb.sh <bundle-dir> <output-dir>
#
#   bundle-dir   The AirPodsDesktop directory produced by make-bundle.sh
#   output-dir   Where the .deb is written

set -euo pipefail

if (( $# < 2 )); then
    sed -n '3,11p' "$0" >&2
    exit 1
fi

BUNDLE=$(realpath "$1")
OUT=$(realpath -m "$2")
APP=AirPodsDesktop
PKG=airpods-desktop
SRC=$(realpath "$(dirname "$0")/../..")
VERSION=$(sed -n 's/.*"version-string": "\(.*\)".*/\1/p' "$SRC/vcpkg.json")
ARCH=amd64

if [[ ! -x "$BUNDLE/$APP" || ! -x "$BUNDLE/bin/$APP" ]]; then
    echo "error: $BUNDLE does not look like a bundle from make-bundle.sh" >&2
    exit 1
fi

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
ROOT="$STAGE/root"

mkdir -p "$ROOT/DEBIAN" "$ROOT/opt" "$ROOT/usr/bin" \
    "$ROOT/usr/share/applications" "$ROOT/usr/share/icons/hicolor/scalable/apps" \
    "$ROOT/usr/share/doc/$PKG"

# The application itself. install.sh is for per-user installs and does not belong in the package.
cp -r "$BUNDLE" "$ROOT/opt/$APP"
rm -f "$ROOT/opt/$APP/install.sh"

ln -s "/opt/$APP/$APP" "$ROOT/usr/bin/$APP"

# Desktop entry: the bundle's copy has Exec=AirPodsDesktop, which resolves through /usr/bin.
cp "$BUNDLE/share/applications/$APP.desktop" "$ROOT/usr/share/applications/"
cp "$BUNDLE/share/icons/hicolor/scalable/apps/$APP.svg" \
    "$ROOT/usr/share/icons/hicolor/scalable/apps/"
cp "$SRC/LICENSE" "$ROOT/usr/share/doc/$PKG/copyright"

# Dependencies: every library the bundle loads that is not bundled itself, mapped to the Debian
# package that provides it. Qt, ICU and FFmpeg come from the bundle, so they are not listed.
export LD_LIBRARY_PATH="$BUNDLE/lib"
declare -A deps
while IFS= read -r file; do
    while IFS= read -r lib; do
        [[ "$lib" == "$BUNDLE"/* ]] && continue
        owner=$(dpkg -S "$(realpath "$lib")" 2>/dev/null | head -n1 | cut -d: -f1 || true)
        if [[ -z "$owner" ]]; then
            echo "warning: no package owns $lib, it will not be declared as a dependency" >&2
            continue
        fi
        deps[$owner]=1
    done < <(ldd "$file" 2>/dev/null | sed -n 's/.*=> \(\/[^ ]*\).*/\1/p')
done < <(find "$BUNDLE/bin" "$BUNDLE/plugins" "$BUNDLE/lib" -type f \( -name '*.so*' -o -path '*/bin/*' \))

# libc is always present, so it is not worth declaring.
unset 'deps[libc6]'
DEPENDS=$(printf '%s\n' "${!deps[@]}" | sort | paste -sd, - | sed 's/,/, /g')

cat > "$ROOT/DEBIAN/control" <<CONTROL
Package: $PKG
Version: $VERSION
Section: sound
Priority: optional
Architecture: $ARCH
Depends: $DEPENDS
Recommends: bluez
Maintainer: AirPods Desktop <airpods-desktop@localhost>
Installed-Size: $(du -sk "$ROOT" | cut -f1)
Homepage: https://github.com/SpriteOvO/AirPodsDesktop
Description: AirPods battery and ear detection for the desktop
 Shows AirPods battery levels and case status, pauses media when the earbuds
 are removed, and opens a window when the case lid is opened.
 .
 The Qt libraries are bundled under /opt/AirPodsDesktop. Bluetooth access goes
 through BlueZ, which must be running.
CONTROL

mkdir -p "$OUT"
DEB="$OUT/${PKG}_${VERSION}_${ARCH}.deb"
dpkg-deb --build --root-owner-group "$ROOT" "$DEB"
echo "Package: $DEB"
