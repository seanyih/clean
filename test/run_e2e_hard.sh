#!/usr/bin/env bash
# Run the hard semantic checks through clean and Lean.
# Usage: bash run_e2e_hard.sh
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
bin="$root/release/clean"
src="$root/test/cases-hard/hard.c"
checks="$root/test/cases-hard/hard.check.lean"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

if [[ ! -x "$bin" ]]; then
    echo "clean binary is missing: $bin" >&2
    echo "Build it first with: make" >&2
    exit 1
fi

if ! command -v lean >/dev/null 2>&1; then
    echo "Lean CLI 'lean' was not found in PATH." >&2
    exit 1
fi

if [[ ! -f "$src" || ! -f "$checks" ]]; then
    echo "Missing hard-test input or check file." >&2
    echo "Expected: $src" >&2
    echo "Expected: $checks" >&2
    exit 1
fi

echo "[1/3] Translating test/cases/hard.c"
"$bin" "$src" -o "$tmp/hard.lean"

echo "[2/3] Combining generated Lean with check.lean"
cat "$tmp/hard.lean" "$checks" > "$tmp/hard.checked.lean"

echo "[3/3] Asking Lean to prove all examples"
if lean "$tmp/hard.checked.lean"; then
    echo "PASS: hard end-to-end checks"
else
    echo "FAIL: generated Lean did not prove all checks" >&2
    exit 1
fi
