#!/bin/sh
# Fetches mbedTLS 3.6.2 into third_party/mbedtls. Uses the release tarball
# rather than git because the tarball ships the generated sources.
set -eu

VERSION=3.6.2
URL="https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$VERSION/mbedtls-$VERSION.tar.bz2"

cd "$(dirname "$0")"
mkdir -p third_party

if [ -f third_party/mbedtls/library/ssl_tls.c ]; then
    echo "mbedTLS already present in third_party/mbedtls"
    exit 0
fi

TARBALL="third_party/mbedtls-$VERSION.tar.bz2"
echo "Downloading $URL"
if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 -o "$TARBALL" "$URL"
elif command -v wget >/dev/null 2>&1; then
    wget -O "$TARBALL" "$URL"
else
    echo "get-deps.sh: need curl or wget" >&2
    exit 1
fi

rm -rf "third_party/mbedtls-$VERSION" third_party/mbedtls
tar -xjf "$TARBALL" -C third_party
mv "third_party/mbedtls-$VERSION" third_party/mbedtls
rm -f "$TARBALL"
echo "mbedTLS $VERSION extracted to third_party/mbedtls"
