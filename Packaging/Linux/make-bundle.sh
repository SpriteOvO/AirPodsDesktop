#!/usr/bin/env bash
#
# Builds a self-contained Linux bundle: the application, the Qt libraries and plugins it loads,
# a launcher, and an install.sh that adds it to the current user's application menu.
#
# Usage: make-bundle.sh <build-dir> <qt-prefix> <output-dir> [extra-lib-dir]
#
#   build-dir      CMake build directory that contains Binary/AirPodsDesktop
#   qt-prefix      Qt installation directory (the one holding lib/ and plugins/)
#   output-dir     Where the bundle directory and the .tar.gz are written
#   extra-lib-dir  Optional directory with libraries Qt does not ship, such as libxcb-cursor
#
# Only libraries under qt-prefix or extra-lib-dir are bundled. Everything else (glibc, GL, X11,
# D-Bus, fontconfig...) is expected from the desktop system.

set -euo pipefail

if (( $# < 3 )); then
    sed -n '3,14p' "$0" >&2
    exit 1
fi

BUILD_DIR=$(realpath "$1")
QT_PREFIX=$(realpath "$2")
OUT=$(realpath -m "$3")
EXTRA_LIB=${4:+$(realpath "$4")}

SRC=$(realpath "$(dirname "$0")/../..")
APP=AirPodsDesktop
BIN="$BUILD_DIR/Binary/$APP"
VERSION=$(sed -n 's/.*"version-string": "\(.*\)".*/\1/p' "$SRC/vcpkg.json")
BUNDLE="$OUT/$APP"

if [[ ! -x "$BIN" ]]; then
    echo "error: $BIN not found, build the project first" >&2
    exit 1
fi

# ldd must resolve Qt and extra libraries from the bundle sources, not from the system.
export LD_LIBRARY_PATH="$QT_PREFIX/lib${EXTRA_LIB:+:$EXTRA_LIB}"

rm -rf "$BUNDLE"
mkdir -p "$BUNDLE"/{bin,lib,plugins,share/$APP/translations,share/applications,share/icons/hicolor/scalable/apps}

# Qt plugin directories the application uses at runtime (platform, Qt Multimedia FFmpeg backend,
# image and icon formats, and the XCB OpenGL integration).
for dir in platforms xcbglintegrations multimedia imageformats iconengines; do
    if [[ -d "$QT_PREFIX/plugins/$dir" ]]; then
        cp -rL "$QT_PREFIX/plugins/$dir" "$BUNDLE/plugins/"
    fi
done

cp -L "$BIN" "$BUNDLE/bin/$APP"

# Walk the dependency graph of the executable and every plugin, copying the libraries that come
# from qt-prefix or extra-lib-dir. ldd prints '=> path' for resolved libraries only.
is_bundled() {
    case "$1" in
        "$QT_PREFIX"/lib/*) return 0 ;;
        *) [[ -n "$EXTRA_LIB" && "$1" == "$EXTRA_LIB"/* ]] ;;
    esac
}

files=("$BUNDLE/bin/$APP")
while IFS= read -r plugin; do
    files+=("$plugin")
done < <(find "$BUNDLE/plugins" -name '*.so*')

i=0
while (( i < ${#files[@]} )); do
    current=${files[$i]}
    i=$((i + 1))
    while IFS= read -r dep; do
        is_bundled "$dep" || continue
        name=$(basename "$dep")
        if [[ ! -e "$BUNDLE/lib/$name" ]]; then
            cp -L "$dep" "$BUNDLE/lib/$name"
            files+=("$BUNDLE/lib/$name")
        fi
    done < <(ldd "$current" 2>/dev/null | sed -n 's/.*=> \(\/[^ ]*\).*/\1/p')
done

# Translations and licenses ship next to the binary so the paths the application checks exist.
cp -L "$BUILD_DIR"/Binary/translations/*.qm "$BUNDLE/share/$APP/translations/"
cp -r "$BUILD_DIR/Binary/licenses" "$BUNDLE/share/$APP/" 2>/dev/null || true
cp "$SRC/LICENSE" "$BUNDLE/share/$APP/"

cp "$SRC/Packaging/Linux/$APP.desktop" "$BUNDLE/share/applications/"
cp "$SRC/Source/Resource/Image/Icon.svg" "$BUNDLE/share/icons/hicolor/scalable/apps/$APP.svg"

cat > "$BUNDLE/$APP" <<'LAUNCHER'
#!/bin/sh
HERE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
export LD_LIBRARY_PATH="$HERE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$HERE/plugins/platforms"
exec "$HERE/bin/AirPodsDesktop" "$@"
LAUNCHER
chmod +x "$BUNDLE/$APP"

cat > "$BUNDLE/install.sh" <<'INSTALLER'
#!/bin/sh
# Installs AirPods Desktop for the current user only (no root needed).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
DEST="$DATA/AirPodsDesktop-app"
APPS="$DATA/applications"
ICONS="$DATA/icons/hicolor/scalable/apps"

rm -rf "$DEST"
mkdir -p "$DEST" "$APPS" "$ICONS"
cp -r "$HERE/bin" "$HERE/lib" "$HERE/plugins" "$HERE/share" "$HERE/AirPodsDesktop" "$DEST/"

sed "s|^Exec=.*|Exec=$DEST/AirPodsDesktop|" "$DEST/share/applications/AirPodsDesktop.desktop" \
    > "$APPS/AirPodsDesktop.desktop"
cp "$DEST/share/icons/hicolor/scalable/apps/AirPodsDesktop.svg" "$ICONS/"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APPS" || true

echo "Installed to $DEST. Start it from the application menu, or run $DEST/AirPodsDesktop."
INSTALLER
chmod +x "$BUNDLE/install.sh"

TARBALL="$OUT/$APP-$VERSION-linux-x64.tar.gz"
tar -C "$OUT" -czf "$TARBALL" "$APP"
echo "Bundle: $BUNDLE"
echo "Archive: $TARBALL"
