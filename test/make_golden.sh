#!/usr/bin/env bash
# test/make_golden.sh NAME... — write the "run" goldens of test/cases/NAME.c.
#
# The oracle is the C program itself (decision D12): compile it with gcc, run
# it on NAME.stdin (if any) with NAME.args (if any), and record what it
# printed. run_e2e.sh then demands the same from the translated program.
#
#   NAME.expected.out    stdout (always written: it switches the run step on)
#   NAME.expected.err    stderr (written only when not empty)
#   NAME.expected.code   exit code (written only when not 0)
#
# Read the files before committing them: gcc is the oracle only where clean
# claims to follow C. A deliberate difference (see the W-lints) must be fixed
# in the C program, not in the golden.

set -u

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cases="$root/test/cases"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

[ "$#" -ge 1 ] || { echo "usage: $0 NAME..." >&2; exit 2; }

for name in "$@"; do
    src="$cases/$name.c"
    [ -f "$src" ] || { echo "no such case: $src" >&2; exit 2; }
    gcc -std=gnu2x -w -x c "$src" -o "$tmp/$name.bin" -lm || { echo "gcc failed on $name" >&2; exit 1; }
    stdin="$cases/$name.stdin"
    [ -f "$stdin" ] || stdin=/dev/null
    args=""
    [ -f "$cases/$name.args" ] && args="$(cat "$cases/$name.args")"
    work="$tmp/run-$name"
    mkdir -p "$work"
    # shellcheck disable=SC2086
    (cd "$work" && "$tmp/$name.bin" $args < "$stdin" > "$tmp/$name.out" 2> "$tmp/$name.err")
    code=$?
    cp "$tmp/$name.out" "$cases/$name.expected.out"
    rm -f "$cases/$name.expected.err" "$cases/$name.expected.code"
    [ -s "$tmp/$name.err" ] && cp "$tmp/$name.err" "$cases/$name.expected.err"
    [ "$code" != 0 ] && echo "$code" > "$cases/$name.expected.code"
    echo "wrote goldens for $name (exit $code)"
done
