#!/bin/bash
# Build pocket-home and wifitest natively and run the integration tests in Docker.
set -euo pipefail
cd "$(dirname "$0")/.."
docker build -q -t pocket-home-test tests >/dev/null
docker run --rm -e HOST_ID="$(id -u):$(id -g)" -v "$PWD:/src" -w /src pocket-home-test bash -euc '
  trap "chown -R \$HOST_ID build Builds/LinuxMakefile/build" EXIT
  P="alsa gio-2.0"
  make -C Builds/LinuxMakefile -f UnitTests.mk -j"$(nproc)" CONFIG=Release DEPFLAGS= \
    TARGET_ARCH=-march=x86-64 PKG_CONFIG_CFLAGS="$(pkg-config --cflags $P)" \
    PKG_CONFIG_LDFLAGS="$(pkg-config --libs $P) -li2c" ../../build/Release/pocket-home ../../build/Release/wifitest
  python3 -m pytest -v -p no:cacheprovider tests "$@"
' bash "$@"
