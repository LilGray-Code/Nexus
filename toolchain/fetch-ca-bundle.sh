#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_ROOT="${NEXUS_BUILD_ROOT:-$ROOT/build}"
DEST="$BUILD_ROOT/nexus-ca-bundle.pem"
TMP="$DEST.tmp"
URL="https://raw.githubusercontent.com/composer/ca-bundle/024ad1476500f7593a6c08061b1273fd44f35158/res/cacert.pem"
SHA256="796f1389bfea13c0ed78f663b765baaff54a508e5483786bd6ece73c5e3e3cbe"

for tool in curl sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || {
        printf 'Missing required tool: %s\n' "$tool" >&2
        exit 1
    }
done

mkdir -p "$BUILD_ROOT"
trap 'rm -f "$TMP"' EXIT HUP INT TERM
curl --fail --location --retry 3 --retry-all-errors "$URL" --output "$TMP"
ACTUAL_SHA256=$(sha256sum "$TMP" | awk '{print $1}')
if [ "$ACTUAL_SHA256" != "$SHA256" ]; then
    printf 'Mozilla CA bundle checksum mismatch; refusing to use it.\n' >&2
    printf 'Expected: %s\nActual:   %s\n' "$SHA256" "$ACTUAL_SHA256" >&2
    exit 1
fi
grep -q -- '-----BEGIN CERTIFICATE-----' "$TMP"
grep -q -- '-----END CERTIFICATE-----' "$TMP"
printf '\0' >> "$TMP"
mv "$TMP" "$DEST"
printf 'Pinned Mozilla CA bundle verified; PEM bytes including NUL: '
wc -c < "$DEST"
