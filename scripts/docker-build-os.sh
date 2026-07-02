#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

IMAGE_NAME="${MY_OS_IMAGE:-my-os-build}"
PLATFORM="${DOCKER_PLATFORM:-linux/amd64}"

echo "[docker] building my-os inside container"
echo "[docker] image: ${IMAGE_NAME}"
echo "[docker] platform: ${PLATFORM}"

docker run \
  --rm \
  -it \
  --platform "${PLATFORM}" \
  -v "${ROOT_DIR}:/work" \
  -w /work \
  "${IMAGE_NAME}" \
  /bin/bash -lc 'make clean && make && make check'
