#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

IMAGE_NAME="${MY_OS_IMAGE:-my-os-build}"
PLATFORM="${DOCKER_PLATFORM:-linux/amd64}"

cd "$ROOT_DIR"

echo "[docker] building image: ${IMAGE_NAME}"
echo "[docker] platform: ${PLATFORM}"

docker build \
  --platform "${PLATFORM}" \
  -t "${IMAGE_NAME}" \
  .

echo
echo "[docker] image built: ${IMAGE_NAME}"
