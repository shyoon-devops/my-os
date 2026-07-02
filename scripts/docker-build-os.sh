#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

IMAGE="${OS_BUILD_IMAGE:-my-os-build}"
PLATFORM="${OS_BUILD_PLATFORM:-linux/amd64}"
DOCKERFILE="${OS_BUILD_DOCKERFILE:-}"

find_dockerfile() {
  if [ -n "$DOCKERFILE" ]; then
    echo "$DOCKERFILE"
    return
  fi

  if [ -f "$ROOT_DIR/Dockerfile" ]; then
    echo "$ROOT_DIR/Dockerfile"
    return
  fi

  if [ -f "$ROOT_DIR/docker/Dockerfile" ]; then
    echo "$ROOT_DIR/docker/Dockerfile"
    return
  fi

  if [ -f "$ROOT_DIR/scripts/Dockerfile" ]; then
    echo "$ROOT_DIR/scripts/Dockerfile"
    return
  fi

  echo ""
}

ensure_image() {
  if docker image inspect "$IMAGE" >/dev/null 2>&1; then
    return
  fi

  local dockerfile
  dockerfile="$(find_dockerfile)"

  if [ -z "$dockerfile" ]; then
    echo "Docker image not found: $IMAGE"
    echo "No Dockerfile found."
    echo ""
    echo "Set one of these:"
    echo "  OS_BUILD_IMAGE=<existing-image> ./scripts/docker-build-os.sh"
    echo "  OS_BUILD_DOCKERFILE=<path> ./scripts/docker-build-os.sh"
    exit 1
  fi

  echo "Building docker image: $IMAGE"
  echo "Dockerfile: $dockerfile"

  docker build \
    --platform "$PLATFORM" \
    -t "$IMAGE" \
    -f "$dockerfile" \
    "$ROOT_DIR"
}

build_command() {
  local mode="${1:-}"

  case "$mode" in
    ""|incremental)
      echo "make && make check"
      ;;
    clean|rebuild)
      echo "make clean && make && make check"
      ;;
    check)
      echo "make check"
      ;;
    shell|sh|bash)
      echo "bash"
      ;;
    make)
      shift || true
      if [ "$#" -eq 0 ]; then
        echo "make"
      else
        printf "make"
        for arg in "$@"; do
          printf " %q" "$arg"
        done
        printf "\n"
      fi
      ;;
    *)
      printf "%q" "$mode"
      shift || true
      for arg in "$@"; do
        printf " %q" "$arg"
      done
      printf "\n"
      ;;
  esac
}

ensure_image

CMD="$(build_command "$@")"

echo "Docker image: $IMAGE"
echo "Platform: $PLATFORM"
echo "Command: $CMD"

USER_ARGS=()
if [ "${OS_BUILD_AS_ROOT:-0}" != "1" ]; then
  USER_ARGS=(--user "$(id -u):$(id -g)")
fi

docker run \
  --rm \
  --platform "$PLATFORM" \
  "${USER_ARGS[@]}" \
  -v "$ROOT_DIR:/workspace" \
  -w /workspace \
  "$IMAGE" \
  bash -lc "$CMD"
