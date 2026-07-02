#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  bash scripts/pull-build-run.sh [options]

Default:
  - pull current branch from the github remote when it exists
  - sync the pulled branch to configured remotes through make push-remotes
  - force rebuild generated user ELFs before building
  - build with ./scripts/docker-build-os.sh
  - restore tracked generated init ELF after build so the next run starts clean
  - do not start QEMU unless a run option is specified

Options:
  --remote <name>       Git remote to pull from. Default: github if present, otherwise origin
  --clean               Run clean build: ./scripts/docker-build-os.sh clean
  --no-build            Pull only, skip build
  --no-sync-remotes     Do not run make push-remotes after pulling
  --run                 Run: make run
  --run-cocoa           Run: make run-cocoa
  --run-curses          Run: make run-curses
  --run-cocoa-serial    Run: make run-cocoa-serial
  --allow-dirty         Allow local uncommitted changes before pulling
  -h, --help            Show this help
USAGE
}

restore_generated_files() {
  if git ls-files --error-unmatch initramfs/bin/init >/dev/null 2>&1; then
    if [ -n "$(git status --porcelain -- initramfs/bin/init)" ]; then
      echo "+ git restore -- initramfs/bin/init"
      git restore -- initramfs/bin/init
    fi
  fi
}

force_rebuild_user_elves() {
  for elf in initramfs/bin/init initramfs/bin/hello initramfs/bin/readkey; do
    if [ -e "$elf" ]; then
      echo "+ rm -f $elf"
      rm -f "$elf"
    fi
  done
}

REMOTE="${MY_OS_REMOTE:-}"
CLEAN=0
BUILD=1
ALLOW_DIRTY=0
SYNC_REMOTES=1
RUN_TARGET=""

while [ "$#" -gt 0 ]; do
  case "$1" in
    --remote)
      if [ "$#" -lt 2 ]; then
        echo "--remote requires a value" >&2
        exit 1
      fi
      REMOTE="$2"
      shift 2
      ;;
    --clean)
      CLEAN=1
      shift
      ;;
    --no-build)
      BUILD=0
      shift
      ;;
    --no-sync-remotes)
      SYNC_REMOTES=0
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
    --allow-dirty)
      ALLOW_DIRTY=1
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

restore_generated_files

BRANCH="$(git branch --show-current)"
if [ -z "$BRANCH" ]; then
  echo "detached HEAD: cannot pull a current branch" >&2
  exit 1
fi

if [ -z "$REMOTE" ]; then
  if git remote get-url github >/dev/null 2>&1; then
    REMOTE="github"
  elif git remote get-url origin >/dev/null 2>&1; then
    REMOTE="origin"
  else
    echo "no usable remote found. Add github or origin remote first." >&2
    exit 1
  fi
fi

if ! git remote get-url "$REMOTE" >/dev/null 2>&1; then
  echo "remote not found: $REMOTE" >&2
  echo "available remotes:" >&2
  git remote -v >&2
  exit 1
fi

echo "repo   : $ROOT"
echo "branch : $BRANCH"
echo "remote : $REMOTE ($(git remote get-url "$REMOTE"))"

if [ "$ALLOW_DIRTY" -ne 1 ]; then
  if [ -n "$(git status --porcelain)" ]; then
    echo "working tree is dirty; refusing to pull" >&2
    echo "commit/stash your changes or pass --allow-dirty" >&2
    git status --short >&2
    exit 1
  fi
fi

echo "+ git fetch $REMOTE $BRANCH"
git fetch "$REMOTE" "$BRANCH"

echo "+ git pull --ff-only $REMOTE $BRANCH"
git pull --ff-only "$REMOTE" "$BRANCH"

if [ "$SYNC_REMOTES" -eq 1 ]; then
  echo "+ make push-remotes"
  make push-remotes
else
  echo "remote sync skipped"
fi

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

restore_generated_files

if [ -n "$RUN_TARGET" ]; then
  echo "+ make $RUN_TARGET"
  make "$RUN_TARGET"
else
  echo "run skipped"
fi
