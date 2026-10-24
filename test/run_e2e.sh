#!/usr/bin/env bash
# test/run_e2e.sh — the end-to-end test (CLAUDE.md rule 4: no unit tests).
#
# For every test/cases/*.c:
#   1. translate it with release/clean;
#   2. diff the output against test/cases/<name>.expected.lean (golden file);
#   3. append test/cases/<name>.checks.lean (Lean `example`s) and type-check
#      the combined file with `lean` — skipped, with a notice, if absent;
#   4. if test/cases/<name>.expected.out exists, RUN the translation
#      (`lean --run`) and compare it with what the C program itself printed:
#        <name>.stdin          standard input        (optional, default empty)
#        <name>.args           command-line words     (optional)
#        <name>.expected.out   expected stdout        (the switch for this step)
#        <name>.expected.err   expected stderr        (optional, default empty)
#        <name>.expected.code  expected exit code     (optional, default 0)
#      The goldens come from the real C program: test/make_golden.sh <name>.
#      The program runs in a scratch directory, so files it writes vanish.
# For every test/cases-should-fail/*.c:
#   expect the translator to exit 1 with a diagnostic on stderr.
# For every test/cases-lint/*.c:
#   translate with --lint=c-semantics; stderr must equal <name>.expected.lint
#   (decision D18), and the .lean output must be identical to a run without
#   the flag: lints only report, they never change the translation.
# For every test/cases-lean-rejects/*.c:
#   the translator must SUCCEED and `lean` must then REJECT the output (code
#   whose legality only Lean can judge, e.g. unproven termination).

set -u

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
bin="$root/release/clean"
cases="$root/test/cases"
fails="$root/test/cases-should-fail"
rejects="$root/test/cases-lean-rejects"
lints="$root/test/cases-lint"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

have_lean=0
command -v lean >/dev/null 2>&1 && have_lean=1

if [ ! -x "$bin" ]; then
    echo "clean binary is missing — run: make"
    exit 1
fi

pass=0
fail=0

for src in "$cases"/*.c; do
    [ -f "$src" ] || continue
    name="$(basename "$src" .c)"

    if ! "$bin" "$src" -o "$tmp/$name.lean" > "$tmp/$name.log" 2>&1; then
        echo "FAIL $name: translator exited with an error"
        sed 's/^/    /' "$tmp/$name.log"
        fail=$((fail + 1))
        continue
    fi
    if grep -q '^W[0-9]' "$tmp/$name.log"; then
        echo "FAIL $name: a lint (W…) was printed without --lint=c-semantics"
        fail=$((fail + 1))
        continue
    fi
    if ! diff -u "$cases/$name.expected.lean" "$tmp/$name.lean" > "$tmp/$name.diff" 2>&1; then
        echo "FAIL $name: output differs from $name.expected.lean"
        sed 's/^/    /' "$tmp/$name.diff"
        fail=$((fail + 1))
        continue
    fi
    if [ "$have_lean" = 1 ] && [ -f "$cases/$name.checks.lean" ]; then
        cat "$tmp/$name.lean" "$cases/$name.checks.lean" > "$tmp/$name.checked.lean"
        if ! (cd "$tmp" && lean "$tmp/$name.checked.lean") > "$tmp/$name.leancheck.log" 2>&1; then
            echo "FAIL $name: generated Lean does not type-check"
            sed 's/^/    /' "$tmp/$name.leancheck.log"
            fail=$((fail + 1))
            continue
        fi
    fi
    if [ -f "$cases/$name.expected.out" ]; then
        if [ "$have_lean" = 0 ]; then
            echo "SKIP $name: run step (lean not installed)"
        else
            work="$tmp/run-$name"
            mkdir -p "$work"
            stdin="$cases/$name.stdin"
            [ -f "$stdin" ] || stdin=/dev/null
            args=""
            [ -f "$cases/$name.args" ] && args="$(cat "$cases/$name.args")"
            # unused-variable warnings would land on stdout; they are not what is tested here
            # shellcheck disable=SC2086
            (cd "$work" && lean -D linter.unusedVariables=false --run "$tmp/$name.lean" $args \
                < "$stdin" > "$tmp/$name.run.out" 2> "$tmp/$name.run.err")
            code=$?
            want_code=0
            [ -f "$cases/$name.expected.code" ] && want_code="$(cat "$cases/$name.expected.code")"
            want_err="$cases/$name.expected.err"
            [ -f "$want_err" ] || want_err=/dev/null
            if ! diff -u "$cases/$name.expected.out" "$tmp/$name.run.out" > "$tmp/$name.diff" 2>&1; then
                echo "FAIL $name: stdout of the translated program differs from the C program's"
                sed 's/^/    /' "$tmp/$name.diff" | head -20
                fail=$((fail + 1))
                continue
            fi
            if ! diff -u "$want_err" "$tmp/$name.run.err" > "$tmp/$name.diff" 2>&1; then
                echo "FAIL $name: stderr of the translated program differs from the C program's"
                sed 's/^/    /' "$tmp/$name.diff" | head -20
                fail=$((fail + 1))
                continue
            fi
            if [ "$code" != "$want_code" ]; then
                echo "FAIL $name: exit code $code, the C program gave $want_code"
                fail=$((fail + 1))
                continue
            fi
        fi
    fi
    echo "PASS $name"
    pass=$((pass + 1))
done

for src in "$fails"/*.c; do
    [ -f "$src" ] || continue
    name="$(basename "$src" .c)"

    if "$bin" "$src" -o "$tmp/$name.lean" > "$tmp/$name.log" 2>&1; then
        echo "FAIL $name: expected a diagnostic, got success"
        fail=$((fail + 1))
        continue
    fi
    if [ ! -s "$tmp/$name.log" ]; then
        echo "FAIL $name: exited nonzero but printed no diagnostic"
        fail=$((fail + 1))
        continue
    fi
    echo "PASS $name (rejected as expected)"
    sed 's/^/    /' "$tmp/$name.log" | head -4
    pass=$((pass + 1))
done

for src in "$lints"/*.c; do
    [ -f "$src" ] || continue
    name="$(basename "$src" .c)"

    if ! "$bin" "$src" -o "$tmp/$name.lint.lean" --lint=c-semantics > /dev/null 2> "$tmp/$name.lint"; then
        echo "FAIL $name: translator exited with an error under --lint=c-semantics"
        sed 's/^/    /' "$tmp/$name.lint"
        fail=$((fail + 1))
        continue
    fi
    if ! diff -u "$lints/$name.expected.lint" "$tmp/$name.lint" > "$tmp/$name.diff" 2>&1; then
        echo "FAIL $name: lint report differs from $name.expected.lint"
        sed 's/^/    /' "$tmp/$name.diff"
        fail=$((fail + 1))
        continue
    fi
    "$bin" "$src" -o "$tmp/$name.plain.lean" > /dev/null 2>&1
    if ! cmp -s "$tmp/$name.lint.lean" "$tmp/$name.plain.lean"; then
        echo "FAIL $name: --lint=c-semantics changed the generated Lean"
        fail=$((fail + 1))
        continue
    fi
    echo "PASS $name (lint report matches, translation unchanged)"
    pass=$((pass + 1))
done

for src in "$rejects"/*.c; do
    [ -f "$src" ] || continue
    name="$(basename "$src" .c)"

    if ! "$bin" "$src" -o "$tmp/$name.lean" > "$tmp/$name.log" 2>&1; then
        echo "FAIL $name: translator should accept this and leave the verdict to Lean"
        sed 's/^/    /' "$tmp/$name.log"
        fail=$((fail + 1))
        continue
    fi
    if [ "$have_lean" = 0 ]; then
        echo "SKIP $name (lean not installed)"
        continue
    fi
    if lean "$tmp/$name.lean" > "$tmp/$name.leancheck.log" 2>&1; then
        echo "FAIL $name: lean accepted code it should reject"
        fail=$((fail + 1))
        continue
    fi
    echo "PASS $name (translated, then rejected by Lean as expected)"
    grep -m1 'error' "$tmp/$name.leancheck.log" | sed 's/^/    /'
    pass=$((pass + 1))
done

echo "----"
echo "passed: $pass   failed: $fail"
[ "$fail" -eq 0 ]
