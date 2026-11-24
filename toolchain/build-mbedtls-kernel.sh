#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_ROOT="${NEXUS_BUILD_ROOT:-$ROOT/build}"
SOURCE="$BUILD_ROOT/deps/mbedtls-4.1.2"
BUILD="$BUILD_ROOT/mbedtls-kernel"

cd "$ROOT"

test -f "$SOURCE/CMakeLists.txt" || {
    printf 'Pinned Mbed TLS source is missing; run toolchain/fetch-mbedtls.sh first.\n' >&2
    exit 1
}

for tool in cmake make; do
    command -v "$tool" >/dev/null 2>&1 || {
        printf 'Missing required tool: %s\n' "$tool" >&2
        exit 1
    }
done

KERNEL_CFLAGS="-std=gnu11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-common -m64 -march=x86-64 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mgeneral-regs-only -mcmodel=kernel -include stdint.h -include stdio.h"

cmake -S "$SOURCE" -B "$BUILD" \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_TESTING=OFF \
    -DENABLE_PROGRAMS=OFF \
    -DMBEDTLS_CONFIG_SET='MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG;MBEDTLS_PLATFORM_TIME_ALT;MBEDTLS_PLATFORM_GMTIME_R_ALT;MBEDTLS_PLATFORM_MS_TIME_ALT;MBEDTLS_PLATFORM_MEMORY;MBEDTLS_PLATFORM_ZEROIZE_ALT;MBEDTLS_PLATFORM_NO_STD_FUNCTIONS;MBEDTLS_SSL_PROTO_TLS1_3' \
    -DMBEDTLS_CONFIG_UNSET='MBEDTLS_PSA_BUILTIN_GET_ENTROPY;MBEDTLS_NET_C;MBEDTLS_TIMING_C;MBEDTLS_FS_IO;MBEDTLS_ENTROPY_NV_SEED;MBEDTLS_AESNI_C;MBEDTLS_PADLOCK_C' \
    -DCMAKE_C_FLAGS="$KERNEL_CFLAGS"

cmake --build "$BUILD" --parallel 2

for archive in \
    "$BUILD/library/libmbedtls.a" \
    "$BUILD/library/libmbedx509.a" \
    "$BUILD/library/libmbedcrypto.a"; do
    test -s "$archive" || {
        printf 'Mbed TLS archive missing: %s\n' "$archive" >&2
        exit 1
    }
done
printf 'Freestanding Mbed TLS 4.1.2 libraries built at %s\n' "$BUILD"
