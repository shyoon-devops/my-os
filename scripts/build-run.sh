#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  bash scripts/build-run.sh [options]

Default:
  - force rebuild generated user ELFs
  - build with ./scripts/docker-build-os.sh (incremental)
  - do not start QEMU unless a run option is specified

Options:
  --clean               Run clean build: ./scripts/docker-build-os.sh clean
  --no-build            Skip build (run only)
  --run                 Run: make run
  --run-cocoa           Run: make run-cocoa
  --run-curses          Run: make run-curses
  --run-cocoa-serial    Run: make run-cocoa-serial
  -h, --help            Show this help
USAGE
}

force_rebuild_user_elves() {
  for elf in initramfs/bin/init initramfs/bin/hello initramfs/bin/readkey; do
    if [ -e "$elf" ]; then
      echo "+ rm -f $elf"
      rm -f "$elf"
    fi
  done
}

CLEAN=0
BUILD=1
RUN_TARGET=""

while [ "$#" -gt 0 ]; do
  case "$1" in
    --clean)
      CLEAN=1
      shift
      ;;
    --no-build)
      BUILD=0
      shift
      ;;
    --run)
      RUN_TARGET="run"
      shift
      ;;
    --run-cocoa)
      RUN_TARGET="run-cocoa"
      shift
      ;;
    --run-curses)
      RUN_TARGET="run-curses"
      shift
      ;;
    --run-cocoa-serial)
      RUN_TARGET="run-cocoa-serial"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

if [ "$BUILD" -eq 1 ]; then
  force_rebuild_user_elves

  if [ "$CLEAN" -eq 1 ]; then
    echo "+ ./scripts/docker-build-os.sh clean"
    ./scripts/docker-build-os.sh clean
  else
    echo "+ ./scripts/docker-build-os.sh"
    ./scripts/docker-build-os.sh
  fi
else
  echo "build skipped"
fi

if [ -n "$RUN_TARGET" ]; then
  echo "+ make $RUN_TARGET"
  make "$RUN_TARGET"
else
  echo "run skipped"
fi
