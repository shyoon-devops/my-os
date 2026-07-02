#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  bash scripts/pull-build-run.sh [options]

Default:
  - pull current branch from the github remote when it exists
  - sync the pulled branch to configured remotes through make push-remotes
  - apply small local build overlays that avoid rewriting large source files
  - build with ./scripts/docker-build-os.sh
  - restore tracked generated/local overlay files after build so the next run starts clean
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

SHELL_COMPLETION_OVERLAY_APPLIED=0

restore_generated_files() {
  if git ls-files --error-unmatch initramfs/bin/init >/dev/null 2>&1; then
    if [ -n "$(git status --porcelain -- initramfs/bin/init)" ]; then
      echo "+ git restore -- initramfs/bin/init"
      git restore -- initramfs/bin/init
    fi
  fi
}

apply_local_build_overlays() {
  if [ ! -f kernel/shell.c ]; then
    return
  fi

  if grep -q '"initrun"' kernel/shell.c; then
    return
  fi

  echo "+ apply local shell completion overlay"

  python3 - <<'PY'
from pathlib import Path

p = Path("kernel/shell.c")
text = p.read_text()

old_commands = '    "elflast",\n    "elfload",\n    "elfinfo"\n};'
new_commands = '    "elflast",\n    "elfload",\n    "elfinfo",\n    "initrun"\n};'

if old_commands not in text:
    raise SystemExit("shell completion list anchor not found")

text = text.replace(old_commands, new_commands, 1)

old_path_cmd = '           shell_streq(command, "elfload");'
new_path_cmd = '           shell_streq(command, "elfload") ||\n           shell_streq(command, "initrun");'

if old_path_cmd in text:
    text = text.replace(old_path_cmd, new_path_cmd, 1)

p.write_text(text)
PY

  SHELL_COMPLETION_OVERLAY_APPLIED=1
}

restore_local_build_overlays() {
  if [ "$SHELL_COMPLETION_OVERLAY_APPLIED" -eq 1 ]; then
    echo "+ git restore -- kernel/shell.c"
    git restore -- kernel/shell.c
  fi
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

apply_local_build_overlays

if [ "$BUILD" -eq 1 ]; then
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

restore_local_build_overlays
restore_generated_files

if [ -n "$RUN_TARGET" ]; then
  echo "+ make $RUN_TARGET"
  make "$RUN_TARGET"
else
  echo "run skipped"
fi
