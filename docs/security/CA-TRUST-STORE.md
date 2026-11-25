# Nexus built-in HTTPS trust store

The TLS-enabled release image embeds the Mozilla public-root CA store converted to PEM by curl's CA Extract service (https://curl.se/ca/cacert.pem). The build script fetches the PEM and its published SHA-256 file over HTTPS, verifies the digest, and rejects files outside expected size and certificate-count bounds before linking the data into the kernel.

The upstream bundle is distributed under the Mozilla Public License 2.0. It contains Mozilla-derived public root certificates only. It does not carry all browser-specific root trust-policy metadata (for example, Firefox-only CA name constraints described by the upstream service), so it is a general PEM trust-anchor set rather than a complete Firefox/Chrome root-program implementation.

The browser prefers a valid NEXUS.CRT PEM file from the writable Nexus Data volume as an administrator override. When no valid override exists, it uses the immutable built-in store. HTTPS remains fail-closed: invalid time, invalid certificate chains, hostname mismatch, handshake errors, and missing TLS support must not downgrade to HTTP.

To refresh the embedded store, rebuild the release ISO with network access so toolchain/fetch-ca-bundle.sh retrieves and verifies the current upstream file. An in-OS trust-store updater should use authenticated update metadata and rollback protection before replacing persistent trust anchors.
