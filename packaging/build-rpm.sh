#!/usr/bin/env bash
# Builds an .rpm using packaging/rpm/ajazzy.spec. Needs to run from a git
# checkout (it archives HEAD to build the source tarball rpmbuild wants).
#
#   packaging/build-rpm.sh 1.2.3
set -euo pipefail

VERSION="${1:?usage: build-rpm.sh <version> [output-dir]}"
OUT="${2:-dist}"

TOPDIR="$(mktemp -d)"
SRCROOT="$(mktemp -d)"
trap 'rm -rf "$TOPDIR" "$SRCROOT"' EXIT

mkdir -p "$TOPDIR"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
mkdir -p "$SRCROOT/ajazzy-$VERSION"
git archive HEAD | tar -x -C "$SRCROOT/ajazzy-$VERSION"
tar czf "$TOPDIR/SOURCES/ajazzy-$VERSION.tar.gz" -C "$SRCROOT" "ajazzy-$VERSION"

# --nodeps: the actual build dependencies (gcc, gtk4-devel, etc) are
# installed via apt in CI, under their Debian names -- rpm has no way to
# know those satisfy the spec's BuildRequires (which use Fedora package
# names) when it's not running on an rpm-based distro, so it would
# otherwise refuse to build here even though everything it needs is
# already present.
rpmbuild \
    --define "_topdir $TOPDIR" \
    --define "_version $VERSION" \
    --nodeps \
    -bb packaging/rpm/ajazzy.spec

mkdir -p "$OUT"
find "$TOPDIR/RPMS" -name '*.rpm' -exec cp {} "$OUT/" \;
echo "built rpm(s) into $OUT/"
