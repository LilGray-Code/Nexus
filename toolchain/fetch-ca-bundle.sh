#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_ROOT="$ROOT/build"
DEPS_DIR="$BUILD_ROOT/deps/ca-bundle"
mkdir -p "$DEPS_DIR"
for tool in curl sha256sum wc grep mktemp mv tr; do
    command -v "$tool" >/dev/null 2>&1 || {
        printf 'Missing required tool: %s\n' "$tool" >&2
        exit 1
    }
done
TMP_DIR=$(mktemp -d "$BUILD_ROOT/.ca-bundle.XXXXXX")
cleanup() { rm -rf "$TMP_DIR"; }
trap cleanup EXIT HUP INT TERM
# The build host verifies curl.se with its normal CA store.
curl --fail --location --retry 3 --retry-all-errors \
    https://curl.se/ca/cacert.pem --output "$TMP_DIR/cacert.pem"
curl --fail --location --retry 3 --retry-all-errors \
    https://curl.se/ca/cacert.pem.sha256 --output "$TMP_DIR/cacert.pem.sha256"
(cd "$TMP_DIR" && sha256sum --check --status cacert.pem.sha256) || {
    printf 'Mozilla CA bundle SHA-256 verification failed.\n' >&2
    exit 1
}
size=$(wc -c < "$TMP_DIR/cacert.pem" | tr -d ' ')
cert_count=$(grep -c '^-----BEGIN CERTIFICATE-----$' "$TMP_DIR/cacert.pem" || true)
if [ "$size" -lt 100000 ] || [ "$size" -gt 524288 ] ||
   [ "$cert_count" -lt 80 ] || [ "$cert_count" -gt 250 ]; then
    printf 'CA bundle outside expected bounds (%s bytes, %s certs).\n' "$size" "$cert_count" >&2
    exit 1
fi
grep -q '^-----BEGIN CERTIFICATE-----$' "$TMP_DIR/cacert.pem"
grep -q '^-----END CERTIFICATE-----$' "$TMP_DIR/cacert.pem"
mv "$TMP_DIR/cacert.pem" "$DEPS_DIR/cacert.pem"
mv "$TMP_DIR/cacert.pem.sha256" "$DEPS_DIR/cacert.pem.sha256"
printf 'Verified Mozilla-derived CA bundle: %s bytes, %s certificates\n' "$size" "$cert_count"
