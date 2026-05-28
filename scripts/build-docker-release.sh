#!/usr/bin/env bash
# scripts/build-docker-release.sh — reproducible Docker release build.
#
# Builds docker/Dockerfile.linux against a pinned-by-digest Ubuntu base,
# extracts the deterministic tarball produced inside, and copies it into
# releases/<version>/. The artifact is byte-identical to what
# scripts/build-linux-release.sh produces on a host with the same base image
# and tool versions — running both is a useful cross-check.
#
# Usage:
#   scripts/build-docker-release.sh [version]
#     version  Tag string (default: v3.0.0).
#
# Output:
#   releases/<version>/radiant-core-linux-x64-<version>.tar.gz
#   releases/<version>/radiant-core-linux-x64-<version>.tar.gz.sha256
#   releases/<version>/radiant-core-docker-amd64-<version>.tar           (image)
#   releases/<version>/radiant-core-docker-amd64-<version>.tar.sha256

set -euo pipefail

VERSION="${1:-v3.0.0}"
HOST_TRIPLE="x86_64-linux-gnu"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$ROOT_DIR"

# --- determinism baseline -----------------------------------------------------
export SOURCE_DATE_EPOCH="$(git log -1 --pretty=%ct HEAD)"
export LC_ALL=C
export TZ=UTC

IMAGE_TAG="radiant-core:${VERSION}-amd64"

echo "================================================================"
echo "Radiant Core Docker release build (reproducible, depends/-based)"
echo "================================================================"
echo "Version           : ${VERSION}"
echo "Image tag         : ${IMAGE_TAG}"
echo "SOURCE_DATE_EPOCH : ${SOURCE_DATE_EPOCH}"

command -v docker >/dev/null 2>&1 || { echo "ERROR: docker is required." >&2; exit 1; }

# Refuse to build if the Dockerfile still has the digest placeholder. The
# Dockerfile carries PINNED_DIGEST_PLACEHOLDER until a maintainer rotates the
# pin, at which point the image is reproducible against a known-good base.
if grep -q PINNED_DIGEST_PLACEHOLDER docker/Dockerfile.linux; then
    cat <<EOF >&2
ERROR: docker/Dockerfile.linux still contains PINNED_DIGEST_PLACEHOLDER.

Reproducible Docker builds require the base image to be pinned by content
digest, not by floating tag. To set the pin, run:

    docker pull ubuntu:24.04
    docker inspect --format='{{index .RepoDigests 0}}' ubuntu:24.04

Replace both occurrences of 'sha256:PINNED_DIGEST_PLACEHOLDER' in
docker/Dockerfile.linux with the resulting digest, commit the change, and
re-run this script.
EOF
    exit 1
fi

# --- step 1: build the image, passing SOURCE_DATE_EPOCH and VERSION ----------
echo
echo ">>> Building Docker image (this also runs depends/ + cmake inside) ..."
DOCKER_BUILDKIT=1 docker build \
    --build-arg VERSION="${VERSION}" \
    --build-arg HOST_TRIPLE="${HOST_TRIPLE}" \
    --build-arg SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH}" \
    -f docker/Dockerfile.linux \
    -t "${IMAGE_TAG}" \
    .

# --- step 2: extract the reproducible Linux tarball produced inside ----------
RELEASE_DIR="${ROOT_DIR}/releases/${VERSION}"
mkdir -p "${RELEASE_DIR}"

echo
echo ">>> Extracting Linux tarball from image ..."
CID="$(docker create "${IMAGE_TAG}")"
trap 'docker rm -f "${CID}" >/dev/null 2>&1 || true' EXIT
docker cp "${CID}:/release/radiant-core-linux-x64-${VERSION}.tar.gz" \
    "${RELEASE_DIR}/radiant-core-linux-x64-${VERSION}.tar.gz"
docker cp "${CID}:/release/radiant-core-linux-x64-${VERSION}.tar.gz.sha256" \
    "${RELEASE_DIR}/radiant-core-linux-x64-${VERSION}.tar.gz.sha256"

# --- step 3: save the runtime image itself as a release artifact -------------
IMAGE_ARTIFACT="${RELEASE_DIR}/radiant-core-docker-amd64-${VERSION}.tar"

echo
echo ">>> Saving runtime image to ${IMAGE_ARTIFACT} ..."
docker save "${IMAGE_TAG}" -o "${IMAGE_ARTIFACT}"
( cd "${RELEASE_DIR}" && sha256sum "$(basename "${IMAGE_ARTIFACT}")" \
    > "$(basename "${IMAGE_ARTIFACT}").sha256" )

echo
echo "================================================================"
echo "Docker release build complete."
echo "Linux tarball : ${RELEASE_DIR}/radiant-core-linux-x64-${VERSION}.tar.gz"
echo "Docker image  : ${IMAGE_ARTIFACT}"
echo "Image tag     : ${IMAGE_TAG}"
echo "================================================================"
