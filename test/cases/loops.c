#include "lean.h"

// loops.c — main-spec §6: range-`for` (§6.1), the general `for` (a `while` with
// the step at the end), `while`, `do … while`, and `break`/`continue`.

// §6.1 range-for: `unsigned` loop variable, `unsigned` bound — the range is Nat
unsigned sum_range(unsigned n) {
    unsigned s = 0;
    for (unsigned i = 0; i < n; i++) s += i;
    return s;
}

// an `int` loop variable over a constant bound: the body sees `(i : Int)`
int sum_digits(void) {
    int s = 0;
    for (int i = 1; i <= 9; i++) s += i;
    return s;
}

// `<=` widens the bound by one; `i += K` becomes [A:B:K] (§6.1)
unsigned sum_odd_steps(unsigned n) {
    unsigned s = 0;
    for (unsigned i = 1; i <= n; i += 3) s += i;
    return s;
}

// a `uintN_t` loop variable: the body sees the range's Nat (§14.2)
unsigned sum_u32(uint32_t n) {
    uint32_t s = 0;
    for (uint32_t i = 1; i <= n; i++) s += i;
    return s;
}

// §14.2: the bound is `int`, so the general form (§6.1.2) is used: the
// initialiser before a `while`, the step at the end of the body
int sum_to(int n) {
    int s = 0;
    for (int i = 1; i <= n; i++) s += i;
    return s;
}

// the body assigns the loop variable → also the general form
unsigned every_other(unsigned n) {
    unsigned s = 0;
    for (unsigned i = 0; i < n; i++) {
        s += 1;
        i += 1;
    }
    return s;
}

// no declaration in the header: the loop variable comes from outside
unsigned over_outer_var(unsigned n) {
    unsigned j = 0;
    unsigned s = 0;
    for (j = 0; j < n; j++) s += 2;
    return s + j;
}

// break and continue inside a range-for
unsigned first_over(unsigned n, unsigned lim) {
    unsigned s = 0;
    for (unsigned i = 0; i < n; i++) {
        if (i >= lim) break;
        if (i % 2 == 1) continue;
        s += i;
    }
    return s;
}

// an early return from inside a loop
int find_index(int a[], int n, int v) {
    for (int i = 0; i < n; i++) {
        if (a[i] == v) return i;
    }
    return -1;
}

// while with break and continue
unsigned count_down(unsigned n) {
    unsigned c = 0;
    while (n > 0) {
        n -= 1;
        if (n == 3) continue;
        if (n > 100) break;
        c += 1;
    }
    return c;
}

// do … while: the body runs first; `continue` inside skips to the test
unsigned until_pair(unsigned n) {
    unsigned c = 0;
    do {
        c += 1;
        if (c % 2 == 0) continue;
        n -= 1;
    } while (n > 0);
    return c;
}

// a `bool` condition, and a loop body that is empty (`pure ()`)
unsigned spin(unsigned n) {
    bool flag = true;
    while (flag) {
        flag = false;
    }
    for (unsigned k = 0; k < 0; k++) ;
    return n;
}

// `for (;;)` — no condition, no step; the exit is a break
unsigned cap(unsigned n) {
    unsigned c = 0;
    for (;;) {
        if (c >= n) break;
        c += 2;
    }
    return c;
}

// nested loops: `break` binds the inner one
unsigned pairs_lt(unsigned n) {
    unsigned c = 0;
    for (unsigned i = 0; i < n; i++) {
        for (unsigned j = 0; j < n; j++) {
            if (j == i) break;
            if (j > i) continue;
            c += 1;
        }
    }
    return c;
}

// a switch inside a loop: the case's `break` closes the case (stripped),
// a `continue` inside a case binds the loop
unsigned classify(unsigned n) {
    unsigned s = 0;
    for (unsigned i = 0; i < n; i++) {
        switch (i % 3) {
            case 0: continue;
            case 1: s += 1; break;
            default: s += 2;
        }
        s += 10;
    }
    return s;
}

// the general form injects its step before every `continue`, at any depth
int skip_threes(int n) {
    int s = 0;
    for (int i = 0; i < n; i++) {
        if (i % 3 == 0) {
            continue;
        }
        s += i;
    }
    return s;
}

// a loop that writes an array: with an `unsigned` bound it stays a range-for,
// and the array writer (D20) still returns the array it filled
void fill_id(int a[], unsigned n) {
    for (unsigned i = 0; i < n; i++) a[i] = i * 2;
}

// comments travel with the loop they sat in
unsigned commented(unsigned n) {
    unsigned c = 0;
    // the loop, about to run
    while (n > 0) {  // n still positive
        n -= 1;  // one less
        // and the closing brace
    }
    return c;
}

// a bool-returning I/O function as a while condition: C re-tests it every
// iteration, and so does the emitted `while (← …)`
bool ask_more(void) {
    printf("ask\n");
    return false;
}

unsigned ask_loop(unsigned n) {
    while (ask_more()) {
        n += 1;
    }
    return n;
}

// loops and console output: the function is `IO` (main-spec §3.3, §8)
void print_row(unsigned n) {
    unsigned i = 0;
    while (i < n) {
        printf("i=%u\n", i);
        i += 1;
    }
    for (unsigned k = 0; k < 2; k++) puts("done");
}
