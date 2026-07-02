#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

echo "[rename] scanning before replace..."
grep -RInI -E 'go-os|go_os|GO_OS|Go OS' . \
  --exclude-dir=.git \
  --exclude-dir=build \
  --exclude-dir=iso \
  --exclude='*.o' \
  --exclude='*.elf' \
  --exclude='*.iso' \
  --exclude='serial.log' \
  --exclude='rename-my-os.sh' || true

echo
echo "[rename] replacing names..."

while IFS= read -r -d '' file; do
  perl -pi -e '
    s/go-os>/my-os>/g;
    s/go-os/my-os/g;
    s/go_os/my_os/g;
    s/GO_OS/MY_OS/g;
    s/Go OS/my-os/g;
  ' "$file"
done < <(
  find . -type f \
    ! -path './.git/*' \
    ! -path './build/*' \
    ! -path './iso/*' \
    ! -path './scripts/rename-my-os.sh' \
    ! -name '*.o' \
    ! -name '*.elf' \
    ! -name '*.iso' \
    ! -name 'serial.log' \
    -print0
)

echo
echo "[rename] scanning after replace..."
grep -RInI -E 'go-os|go_os|GO_OS|Go OS' . \
  --exclude-dir=.git \
  --exclude-dir=build \
  --exclude-dir=iso \
  --exclude='*.o' \
  --exclude='*.elf' \
  --exclude='*.iso' \
  --exclude='serial.log' \
  --exclude='rename-my-os.sh' || true

echo
echo "[rename] done"
