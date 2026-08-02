#!/usr/bin/env bash
# Builds two standalone tarballs for people who just want a binary and
# don't want a .deb/.rpm touching their package database: one with just
# ajazzyctl, one with just ajazzy-gui plus the data files it needs
# (icon, .desktop entry, translation). Both carry a copy of the udev
# rule since that's the one file everyone needs regardless of which
# binary they're after.
set -euo pipefail

VERSION="${1:?usage: build-tarballs.sh <version> [output-dir]}"
OUT="${2:-dist}"
mkdir -p "$OUT"

CLI_STAGE="$(mktemp -d)"
trap 'rm -rf "$CLI_STAGE"' EXIT
CLI_NAME="ajazzy-cli-$VERSION"
mkdir -p "$CLI_STAGE/$CLI_NAME"
install -m755 ajazzyctl "$CLI_STAGE/$CLI_NAME/"
install -m644 udev/71-ajazzy.rules "$CLI_STAGE/$CLI_NAME/"
install -m644 README.md "$CLI_STAGE/$CLI_NAME/"
tar czf "$OUT/$CLI_NAME-linux-x86_64.tar.gz" -C "$CLI_STAGE" "$CLI_NAME"

if [ -f ajazzy-gui ]; then
    GUI_STAGE="$(mktemp -d)"
    GUI_NAME="ajazzy-gui-$VERSION"
    mkdir -p "$GUI_STAGE/$GUI_NAME/icons" "$GUI_STAGE/$GUI_NAME/locale/pt_BR/LC_MESSAGES"
    install -m755 ajazzy-gui "$GUI_STAGE/$GUI_NAME/"
    install -m644 gui/icons/io.github.ajazzy.Gui.svg "$GUI_STAGE/$GUI_NAME/icons/"
    install -m644 gui/io.github.ajazzy.Gui.desktop "$GUI_STAGE/$GUI_NAME/"
    [ -f locale/pt_BR/LC_MESSAGES/ajazzy.mo ] && install -m644 locale/pt_BR/LC_MESSAGES/ajazzy.mo \
        "$GUI_STAGE/$GUI_NAME/locale/pt_BR/LC_MESSAGES/"
    install -m644 udev/71-ajazzy.rules "$GUI_STAGE/$GUI_NAME/"
    install -m644 README.md "$GUI_STAGE/$GUI_NAME/"
    tar czf "$OUT/$GUI_NAME-linux-x86_64.tar.gz" -C "$GUI_STAGE" "$GUI_NAME"
    rm -rf "$GUI_STAGE"
fi

echo "built tarballs into $OUT/"
