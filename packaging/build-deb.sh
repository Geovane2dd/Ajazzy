#!/usr/bin/env bash
# Builds a .deb from binaries that are already compiled in this checkout
# (run `make` first). Used by .github/workflows/release.yml, but works
# fine by hand too:
#
#   make
#   packaging/build-deb.sh 1.2.3
#
# The version number is passed in rather than read from a VERSION file
# because the release workflow computes it fresh from git tags each run.
set -euo pipefail

VERSION="${1:?usage: build-deb.sh <version> [output-dir]}"
OUT="${2:-dist}"
ARCH="$(dpkg --print-architecture 2>/dev/null || echo amd64)"
ROOT="$(mktemp -d)"
trap 'rm -rf "$ROOT"' EXIT

mkdir -p "$ROOT/DEBIAN" \
         "$ROOT/usr/bin" \
         "$ROOT/usr/share/applications" \
         "$ROOT/usr/share/icons/hicolor/scalable/apps" \
         "$ROOT/usr/share/locale/pt_BR/LC_MESSAGES" \
         "$ROOT/lib/udev/rules.d"

install -m755 ajazzyctl "$ROOT/usr/bin/ajazzyctl"

DEPENDS="libc6"
if [ -f ajazzy-gui ]; then
    install -m755 ajazzy-gui "$ROOT/usr/bin/ajazzy-gui"
    install -m644 gui/io.github.ajazzy.Gui.desktop "$ROOT/usr/share/applications/"
    install -m644 gui/icons/io.github.ajazzy.Gui.svg "$ROOT/usr/share/icons/hicolor/scalable/apps/"
    DEPENDS="$DEPENDS, libgtk-4-1, libadwaita-1-0"
fi

if [ -f locale/pt_BR/LC_MESSAGES/ajazzy.mo ]; then
    install -m644 locale/pt_BR/LC_MESSAGES/ajazzy.mo "$ROOT/usr/share/locale/pt_BR/LC_MESSAGES/"
fi

install -m644 udev/71-ajazzy.rules "$ROOT/lib/udev/rules.d/"

cat > "$ROOT/DEBIAN/control" <<EOF
Package: ajazzy
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Depends: $DEPENDS
Maintainer: Ajazzy contributors
Description: Configure AJAZZ gaming mice on Linux
 A reverse-engineered CLI (ajazzyctl) and GTK4 GUI (ajazzy-gui) for
 setting DPI, report rate, RGB lighting, button remapping and macros
 on AJAZZ mice, without the official Windows-only driver.
EOF

cat > "$ROOT/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
# picks up the new udev rule immediately instead of waiting for the
# next reboot -- users still need to unplug/replug the mouse (or
# re-login) for the permission change to actually take effect
if command -v udevadm >/dev/null 2>&1; then
    udevadm control --reload-rules || true
fi
EOF
chmod 755 "$ROOT/DEBIAN/postinst"

mkdir -p "$OUT"
dpkg-deb --build --root-owner-group "$ROOT" "$OUT/ajazzy_${VERSION}_${ARCH}.deb"
echo "built $OUT/ajazzy_${VERSION}_${ARCH}.deb"
