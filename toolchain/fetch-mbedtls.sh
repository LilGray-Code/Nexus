#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_ROOT="${NEXUS_BUILD_ROOT:-$ROOT/build}"
DEPS_DIR="$BUILD_ROOT/deps"
VERSION=4.1.2
ARCHIVE="$DEPS_DIR/mbedtls-$VERSION.tar.bz2"
DEST="$DEPS_DIR/mbedtls-$VERSION"
URL="https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$VERSION/mbedtls-$VERSION.tar.bz2"
SHA256="392215e5e4846df04e2f4c4516079303bd73edde837d8f3e68fb4d4d7ddf0126"

for tool in curl sha256sum tar mktemp; do
    command -v "$tool" >/dev/null 2>&1 || {
        printf 'Missing required tool: %s\n' "$tool" >&2
        exit 1
    }
done

mkdir -p "$DEPS_DIR"
TMP_DIR=$(mktemp -d "$DEPS_DIR/.mbedtls-$VERSION.XXXXXX")
cleanup() {
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT HUP INT TERM

if [ ! -f "$ARCHIVE" ]; then
    curl --fail --location --retry 3 --retry-all-errors \
        "$URL" --output "$TMP_DIR/mbedtls-$VERSION.tar.bz2"
    mv "$TMP_DIR/mbedtls-$VERSION.tar.bz2" "$ARCHIVE"
fi

printf '%s  %s\n' "$SHA256" "$ARCHIVE" | sha256sum --check --status || {
    printf 'Mbed TLS archive checksum mismatch; refusing to extract.\n' >&2
    rm -f "$ARCHIVE"
    exit 1
}

if [ -f "$DEST/include/mbedtls/ssl.h" ] &&
   [ -f "$DEST/include/mbedtls/build_info.h" ] &&
   [ -d "$DEST/tf-psa-crypto" ]; then
    printf 'Verified Mbed TLS %s source already present at %s\n' "$VERSION" "$DEST"
    exit 0
fi

mkdir -p "$TMP_DIR/source"
tar --extract --bzip2 --file "$ARCHIVE" --directory "$TMP_DIR/source" \
    --strip-components=1

test -f "$TMP_DIR/source/CMakeLists.txt"
test -f "$TMP_DIR/source/include/mbedtls/ssl.h"
test -f "$TMP_DIR/source/include/mbedtls/build_info.h"
test -d "$TMP_DIR/source/tf-psa-crypto"

rm -rf "$DEST"
mv "$TMP_DIR/source" "$DEST"
printf 'Verified Mbed TLS %s source installed at %s\n' "$VERSION" "$DEST"
