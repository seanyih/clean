#include "lean.h"

// a local, an assignment inside `if`, and a comment inside the body
int abs_val(int x) {
    int r = x;
    if (x < 0) {
        r = -x;  // flip the sign
    }
    // the answer
    return r;
}

// if / else if / else without braces
int sign(int x) {
    if (x > 0) return 1;
    else if (x < 0) return -1;
    else return 0;
}

// compound assignment, ++, and a parameter that is assigned
int sum_to_four(int n) {
    int acc = 0;
    acc += n;
    acc *= 2;
    n++;
    acc = acc + n;
    return acc;
}

// tests: || with a nested if, early return
int clamp(int x, int lo, int hi) {
    if (x < lo || x > hi) {
        if (x < lo) return lo;
        return hi;
    }
    return x;
}

// a bare number is a test (non-zero), plus ! && == !=
int truthy(int x, int y) {
    if (x && !y) return 1;
    if (!(x == y) && x != 0) return 2;
    return 0;
}

// an inner `if` body may shadow an outer local (it is a real scope in Lean)
int shadow(int x) {
    int y = x + 1;
    if (x > 0) {
        int y = 100;
        x = x + y;
    }
    return x + y;
}

// a bare block is flattened; its locals must not hide outer ones
int blocky(int x) {
    {
        int t = x + 1;
        x = t * 2;
    }
    return x;
}

// branches that only declare, or are empty
int empty_branches(int x) {
    if (x) { }
    else { int unused = 1; }
    return x;
}

// calls inside tests; the callee is defined below its caller
int both_positive_doubled(int x) {
    if (sign(x) > 0 && sign(x + 1) > 0) return twice_of(x);
    return 0;
}
int twice_of(int x) { return 2 * x; }

// a body that is only a return keeps the short form (comment moves above)
int one(void) {
    // just one
    return 1;
}
