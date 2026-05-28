#!/usr/bin/env bash
# scripts/build-linux-release.sh — reproducible Linux x86_64 release build.
#
# Uses depends/ to build all C/C++ dependencies from pinned source archives
# (sha256-verified by depends/packages/*.mk), then builds radiantd, radiant-cli,
# radiant-tx, and radiant-qt against the depends prefix using the
# cmake/platforms/Linux64.cmake toolchain file.
#
# Designed to be re-runnable and to produce a byte-identical tarball across
# clean rebuilds from the same commit on the same host. See
# doc/build-reproducibility.md for verification instructions.
#
# Usage:
#   scripts/build-linux-release.sh [version]
#     version  Tag string to embed in the artifact filename (default: v3.0.0)
#
# Output:
#   releases/<version>/radiant-core-linux-x64-<version>.tar.gz
#   releases/<version>/radiant-core-linux-x64-<version>.tar.gz.sha256

set -euo pipefail

VERSION="${1:-v3.0.0}"
HOST_TRIPLE="x86_64-linux-gnu"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$ROOT_DIR"

# --- determinism baseline -----------------------------------------------------
# SOURCE_DATE_EPOCH pins every embedded timestamp (object files, ar metadata,
# tar headers, gzip stream) to the commit timestamp. The same commit always
# produces the same epoch, so two clean builds at the same SHA produce
# byte-identical artifacts.
export SOURCE_DATE_EPOCH="$(git log -1 --pretty=%ct HEAD)"
export LC_ALL=C
export TZ=UTC
umask 0022

echo "================================================================"
echo "Radiant Core Linux release build (reproducible, depends/ + cmake)"
echo "================================================================"
echo "Version           : ${VERSION}"
echo "Host triple       : ${HOST_TRIPLE}"
echo "SOURCE_DATE_EPOCH : ${SOURCE_DATE_EPOCH}"
echo "Working tree      : ${ROOT_DIR}"

# --- host toolchain sanity ----------------------------------------------------
for cmd in cmake ninja make gcc g++ tar gzip sha256sum git python3 curl pkg-config; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "ERROR: '$cmd' is required on the build host." >&2
        exit 1
    fi
done

# --- step 1: build depends/ for the target triple -----------------------------
# depends/ produces a self-contained prefix under depends/${HOST_TRIPLE}/ that
# CMake's Linux64.cmake toolchain file already points at via CMAKE_FIND_ROOT_PATH
# and CMAKE_PREFIX_PATH. Re-running is cheap once the sources are downloaded
# and the prefix is built — only stale recipes get rebuilt.
echo
echo ">>> Building depends/ prefix for ${HOST_TRIPLE} ..."
make -C depends HOST="${HOST_TRIPLE}" NO_QT=1 -j"$(nproc)"

DEPENDS_PREFIX="${ROOT_DIR}/depends/${HOST_TRIPLE}"
if [ ! -d "${DEPENDS_PREFIX}" ]; then
    echo "ERROR: depends/ build did not produce ${DEPENDS_PREFIX}" >&2
    exit 1
fi

# --- step 2: configure + build via the platform toolchain file ----------------
BUILD_DIR="${ROOT_DIR}/build-${HOST_TRIPLE}-release"
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

echo
echo ">>> Configuring cmake (Linux64 toolchain, depends prefix) ..."
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${ROOT_DIR}/cmake/platforms/Linux64.cmake" \
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
echo ">>> Building radiantd / radiant-cli / radiant-tx ..."
cmake --build "${BUILD_DIR}" --target radiantd radiant-cli radiant-tx -j"$(nproc)"

# --- step 3: stage binaries (stripped, deterministic) -------------------------
STAGE_DIR="${ROOT_DIR}/build-${HOST_TRIPLE}-stage/radiant-core-linux-x64"
rm -rf "${STAGE_DIR}"
mkdir -p "${STAGE_DIR}"

cp "${BUILD_DIR}/src/radiantd"    "${STAGE_DIR}/"
cp "${BUILD_DIR}/src/radiant-cli" "${STAGE_DIR}/"
cp "${BUILD_DIR}/src/radiant-tx"  "${STAGE_DIR}/"

# Strip with deterministic flags. --strip-all removes everything not needed at
# runtime; --remove-section=.comment removes the GCC version banner which can
# vary across compilers.
for bin in "${STAGE_DIR}"/*; do
    "${HOST_TRIPLE}-strip" --strip-all --remove-section=.comment --remove-section=.note "${bin}" \
        || strip --strip-all --remove-section=.comment --remove-section=.note "${bin}"
done

GIT_SHA="$(git rev-parse --short HEAD)"
cat > "${STAGE_DIR}/README.txt" <<EOF
Radiant Core ${VERSION} — Linux x86_64
========================================

Files:
  radiantd      — node daemon (wallet RPC enabled)
  radiant-cli   — RPC client
  radiant-tx    — transaction tool

System requirements:
  Linux x86_64, glibc 2.17 or newer

Quick start:
  ./radiantd -server -txindex=1
  ./radiant-cli getblockchaininfo

Build provenance:
  Built reproducibly from depends/ at commit ${GIT_SHA}.
  See doc/build-reproducibility.md in the source tree for the
  verification procedure.

For more information: https://radiantblockchain.org
EOF

# --- step 4: deterministic tarball --------------------------------------------
RELEASE_DIR="${ROOT_DIR}/releases/${VERSION}"
mkdir -p "${RELEASE_DIR}"
ARTIFACT="${RELEASE_DIR}/radiant-core-linux-x64-${VERSION}.tar.gz"

echo
echo ">>> Packing deterministic tarball ..."
# --sort=name        — stable file order
# --owner/group=0    — no UID/GID leakage
# --numeric-owner    — no /etc/passwd lookup
# --mtime=@SDE       — pin mtimes
# --pax-option       — disable pax extended headers that embed timestamps
# pipe to `gzip -n`  — strip the original filename + mtime from gzip header
tar --sort=name \
    --owner=0 --group=0 --numeric-owner \
    --mtime="@${SOURCE_DATE_EPOCH}" \
    --pax-option=exthdr.name=%d/PaxHeaders/%f,delete=atime,delete=ctime \
    -C "$(dirname "${STAGE_DIR}")" \
    -cf - "$(basename "${STAGE_DIR}")" \
    | gzip -9n > "${ARTIFACT}"

# --- step 5: per-artifact sha256 ----------------------------------------------
( cd "${RELEASE_DIR}" && sha256sum "$(basename "${ARTIFACT}")" > "$(basename "${ARTIFACT}").sha256" )

echo
echo "================================================================"
echo "Linux release build complete."
echo "Artifact : ${ARTIFACT}"
echo "Checksum : $(cat "${ARTIFACT}.sha256")"
echo "================================================================"
