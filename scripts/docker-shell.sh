#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

IMAGE_NAME="${MY_OS_IMAGE:-my-os-build}"
PLATFORM="${DOCKER_PLATFORM:-linux/amd64}"

echo "[docker] running shell in image: ${IMAGE_NAME}"
echo "[docker] project mounted at /work"

docker run \
  --rm \
  -it \
  --platform "${PLATFORM}" \
  -v "${ROOT_DIR}:/work" \
  -w /work \
  "${IMAGE_NAME}" \
  /bin/bash
