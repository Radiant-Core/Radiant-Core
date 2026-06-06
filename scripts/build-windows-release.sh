#!/usr/bin/env bash
# scripts/build-windows-release.sh — reproducible Windows x64 release build.
#
# Cross-compiles for x86_64-w64-mingw32 from a Linux build host using the
# mingw-w64 toolchain plus depends/. Produces a deterministic .zip artifact.
#
# Requires (on the Linux build host):
#   - mingw-w64 cross toolchain (x86_64-w64-mingw32-gcc, -g++, -windres, -strip)
#   - the standard depends/ host requirements (cmake, ninja, make, python3,
#     curl, ca-certificates, pkg-config, zip, sha256sum)
#
# Ubuntu/Debian quick install:
#   sudo apt-get install -y g++-mingw-w64-x86-64 mingw-w64-tools \
#       cmake ninja-build make python3 curl ca-certificates pkg-config zip
#
# Usage:
#   scripts/build-windows-release.sh [version]
#     version  Tag string (default: v3.1.0).
#
# Output:
#   releases/<version>/radiant-core-windows-x64-<version>.zip
#   releases/<version>/radiant-core-windows-x64-<version>.zip.sha256

set -euo pipefail

VERSION="${1:-v3.1.0}"
HOST_TRIPLE="x86_64-w64-mingw32"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$ROOT_DIR"

# --- determinism baseline -----------------------------------------------------
export SOURCE_DATE_EPOCH="$(git log -1 --pretty=%ct HEAD)"
export LC_ALL=C
export TZ=UTC
umask 0022

echo "================================================================"
echo "Radiant Core Windows release build (mingw-w64 cross, depends/)"
echo "================================================================"
echo "Version           : ${VERSION}"
echo "Host triple       : ${HOST_TRIPLE}"
echo "SOURCE_DATE_EPOCH : ${SOURCE_DATE_EPOCH}"

# --- host toolchain sanity ----------------------------------------------------
# `posix` thread model in mingw is required by libstdc++; check for the
# posix-flavored gcc binary explicitly to fail fast with a clear message.
REQUIRED_TOOLS=(
    "${HOST_TRIPLE}-gcc-posix"
    "${HOST_TRIPLE}-g++-posix"
    "${HOST_TRIPLE}-windres"
    "${HOST_TRIPLE}-strip"
    cmake ninja make python3 curl tar gzip zip sha256sum git pkg-config
)
for cmd in "${REQUIRED_TOOLS[@]}"; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "ERROR: '$cmd' is required on the Linux build host." >&2
        echo "Install hint: sudo apt-get install -y g++-mingw-w64-x86-64 mingw-w64-tools cmake ninja-build python3 curl pkg-config zip" >&2
        exit 1
    fi
done

# Debian's update-alternatives sometimes points the unsuffixed name at the
# win32-threads flavor. Force posix-threads to avoid std::thread surprises.
update-alternatives --query "${HOST_TRIPLE}-gcc" >/dev/null 2>&1 \
    && sudo update-alternatives --set "${HOST_TRIPLE}-gcc"  "$(command -v "${HOST_TRIPLE}-gcc-posix")"  || true
update-alternatives --query "${HOST_TRIPLE}-g++" >/dev/null 2>&1 \
    && sudo update-alternatives --set "${HOST_TRIPLE}-g++"  "$(command -v "${HOST_TRIPLE}-g++-posix")"  || true

# --- step 1: depends/ ---------------------------------------------------------
echo
echo ">>> Building depends/ prefix for ${HOST_TRIPLE} ..."
make -C depends HOST="${HOST_TRIPLE}" NO_QT=1 -j"$(nproc)"

DEPENDS_PREFIX="${ROOT_DIR}/depends/${HOST_TRIPLE}"
if [ ! -d "${DEPENDS_PREFIX}" ]; then
    echo "ERROR: depends/ build did not produce ${DEPENDS_PREFIX}" >&2
    exit 1
fi

# --- step 2: cmake configure + build ------------------------------------------
BUILD_DIR="${ROOT_DIR}/build-${HOST_TRIPLE}-release"
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

echo
echo ">>> Configuring cmake (Win64 toolchain, depends prefix) ..."
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${ROOT_DIR}/cmake/platforms/Win64.cmake" \
    -DCMAKE_TOOLCHAIN_PREFIX="${HOST_TRIPLE}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_RADIANT_DAEMON=ON \
    -DBUILD_RADIANT_CLI=ON \
    -DBUILD_RADIANT_TX=ON \
    -DBUILD_RADIANT_WALLET=ON \
    -DBUILD_RADIANT_QT=OFF \
    -DBUILD_RADIANT_ZMQ=ON \
    -DENABLE_HARDENING=ON \
    -DENABLE_UPNP=OFF

echo
echo ">>> Building radiantd / radiant-cli / radiant-tx (.exe) ..."
cmake --build "${BUILD_DIR}" --target radiantd radiant-cli radiant-tx -j"$(nproc)"

# --- step 3: stage ------------------------------------------------------------
STAGE_DIR="${ROOT_DIR}/build-${HOST_TRIPLE}-stage/radiant-core-windows-x64"
rm -rf "${STAGE_DIR}"
mkdir -p "${STAGE_DIR}"

cp "${BUILD_DIR}/src/radiantd.exe"    "${STAGE_DIR}/"
cp "${BUILD_DIR}/src/radiant-cli.exe" "${STAGE_DIR}/"
cp "${BUILD_DIR}/src/radiant-tx.exe"  "${STAGE_DIR}/"

for bin in "${STAGE_DIR}"/*.exe; do
    "${HOST_TRIPLE}-strip" --strip-all --remove-section=.comment --remove-section=.note "${bin}"
done

GIT_SHA="$(git rev-parse --short HEAD)"
cat > "${STAGE_DIR}/README.txt" <<EOF
Radiant Core ${VERSION} — Windows x64
=======================================

Files:
  radiantd.exe      — node daemon (wallet RPC enabled)
  radiant-cli.exe   — RPC client
  radiant-tx.exe    — transaction tool

System requirements:
  Windows 10 or Windows 11, x64.

Quick start (PowerShell or cmd):
  radiantd.exe -server -txindex=1
  radiant-cli.exe getblockchaininfo

Build provenance:
  Cross-compiled from Linux with mingw-w64 + depends/ at commit ${GIT_SHA}.
  See doc/build-reproducibility.md in the source tree for the
  verification procedure.

For more information: https://radiantblockchain.org
EOF

# --- step 4: deterministic zip ------------------------------------------------
RELEASE_DIR="${ROOT_DIR}/releases/${VERSION}"
mkdir -p "${RELEASE_DIR}"
ARTIFACT="${RELEASE_DIR}/radiant-core-windows-x64-${VERSION}.zip"
rm -f "${ARTIFACT}"

echo
echo ">>> Packing deterministic zip ..."
# `zip -X` strips extra file attributes (uid/gid/extended attrs). The mtime
# of every entry is set explicitly via touch before zipping.
find "${STAGE_DIR}" -exec touch -d "@${SOURCE_DATE_EPOCH}" {} +
( cd "$(dirname "${STAGE_DIR}")" \
    && find "$(basename "${STAGE_DIR}")" -print0 | LC_ALL=C sort -z \
    | xargs -0 zip -X -q -9 "${ARTIFACT}" )

# --- step 5: per-artifact sha256 ----------------------------------------------
( cd "${RELEASE_DIR}" && sha256sum "$(basename "${ARTIFACT}")" \
    > "$(basename "${ARTIFACT}").sha256" )

echo
echo "================================================================"
echo "Windows release build complete."
echo "Artifact : ${ARTIFACT}"
echo "Checksum : $(cat "${ARTIFACT}.sha256")"
echo "================================================================"
