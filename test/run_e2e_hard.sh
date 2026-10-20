#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
bin="$root/release/clean"
src="$root/test/cases-hard/matrix_power_hard.c"
checks="$root/test/cases-hard/matrix_power_hard.checks.lean"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

if [[ ! -x "$bin" ]]; then
    echo "Missing executable: $bin" >&2
    echo "Build first, e.g. make" >&2
    exit 1
fi
if ! command -v lean >/dev/null 2>&1; then
    echo "Lean CLI 'lean' was not found in PATH." >&2
    exit 1
fi

echo "[1/3] Translating matrix_power_hard.c"
"$bin" "$src" -o "$tmp/matrix_power_hard.lean"

echo "[2/3] Appending Lean proof checks"
cat "$tmp/matrix_power_hard.lean" "$checks" > "$tmp/matrix_power_hard.checked.lean"

echo "[3/3] Running Lean"
lean "$tmp/matrix_power_hard.checked.lean"
echo "PASS: matrix_power_hard"
