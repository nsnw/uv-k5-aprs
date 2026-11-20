#!/bin/sh
# Build the firmware in a container. Usage: ./build.sh [make args...]
# Set DOCKER_CONTEXT (e.g. colima) to choose a Docker context.
set -eu
cd "$(dirname "$0")"
IMAGE=uv-k5-toolchain
DOCKER="docker ${DOCKER_CONTEXT:+--context $DOCKER_CONTEXT}"

if ! $DOCKER image inspect "$IMAGE" >/dev/null 2>&1; then
    $DOCKER build -t "$IMAGE" .
fi

BUILD_ID=$(git describe --always --dirty 2>/dev/null || echo dev)   # the version is in the Makefile
exec $DOCKER run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" "$IMAGE" make BUILD_ID="$BUILD_ID" "$@"
