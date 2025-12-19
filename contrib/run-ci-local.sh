#!/bin/bash
# Script to run the Radiant Node CI build locally using Docker
# Usage: ./contrib/run-ci-local.sh

export LC_ALL=C
set -e

echo ">>> Building CI Docker image..."
docker build -f Dockerfile.ci -t radiant-node-ci .

echo ">>> Starting Build in Container..."
# Mount the current directory to /source in the container
# Copy to a separate directory to avoid polluting the host
docker run --rm -it \
    -v "$(pwd):/source" \
    radiant-node-ci \
    bash -c "
        cp -r /source /build_dir && \
        cd /build_dir && \
        echo '>>> Initializing git repo for lint checks...' && \
        git init -q && \
        echo 'build-ci/' >> .gitignore && \
        echo 'build-local/' >> .gitignore && \
        git add -A && \
        git config user.email 'ci@radiant.local' && \
        git config user.name 'CI' && \
        git commit -q -m 'CI snapshot' --allow-empty && \
        echo '>>> Configuring CMake...' && \
        rm -rf build-ci && \
        mkdir -p build-ci && \
        cd build-ci && \
        cmake -GNinja .. -DBUILD_RADIANT_QT=OFF && \
        echo '>>> Compiling...' && \
        ninja -j 2 && \
        echo '>>> Running Tests...' && \
        ctest --output-on-failure > check_output.log 2>&1 || (cat check_output.log && exit 1)
    "

echo ">>> Build and Tests Completed Successfully!"
