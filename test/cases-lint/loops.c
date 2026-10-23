#include "lean.h"

// loops.c (lint) — what `--lint=c-semantics` says about loops: the same lints
// as elsewhere (the loop shapes themselves are not lints; C and Lean agree
// there modulo the already-reported arithmetic differences).

// W001: the `int` loop variable (a range-for's variable is still an `int`);
// W009/W010: `a[i]` with an `int` index inside the loop
int sum_ints(int a[], int n) {
    int s = 0;
    for (int i = 0; i < n; i++) {
        s += a[i];
    }
    return s;
}

// W002: `n -= 1` truncates at 0; W005: the divisor may be zero
unsigned churn(unsigned n) {
    unsigned c = 0;
    while (n > 0) {
        c += 1;
        n -= 1;
        n /= c;
    }
    return c;
}

// W009: an `int` used as an index; W010: the write ignores a bad one
void zap(int a[], int n) {
    int i = 0;
    while (i < n) {
        a[i] = 0;
        i += 1;
    }
}
