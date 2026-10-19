#include "lean.h"

// array_writer.c — a `void` function that writes an array parameter returns the
// modified array, and every call site re-assigns what it passed (OD11 c, D20).

void fill(int a[], int n) {            // writes, then falls off the end
    a[0] = n;
    a[1] = n + 1;
}

void bump_at(int a[], int i, int v) {  // writes through an index parameter
    a[i] = a[i] + v;
}

void zero_head(int a[], int n) {       // an early `return;` guard
    if (n == 0) return;
    a[0] = 0;
}

void set_either(int a[], int n) {      // both branches write, then fall through
    if (n > 0) a[0] = 1;
    else a[1] = 1;
}

void set_last(int a[], int n) {        // an `if` without `else` ends the body
    if (n > 0) a[0] = 5;
}

void clamp_low(int a[], int n) {       // `else if` chain without a final `else`
    if (n < 0) a[0] = 0;
    else if (n > 9) a[0] = 9;
}

void by_switch(int a[], int n) {       // a switch: every arm returns the array
    switch (n) {
        case 0: a[0] = 1; break;
        default: a[1] = 2; break;
    }
}

void via_ptr(int *a, int n) {          // `T *a` + a length: the same rule (§4.6)
    a[3] = n;
}

void fill_row(int m[][2], int n) {     // nested arrays: the whole matrix returns
    m[0][0] = n;
    m[1][1] = n;
}

void wrap(int a[], int n) {            // a writer calling a writer
    fill(a, n);
    a[2] = 9;
}

void outer(int a[], int n) {           // the write arrives only through a call
    wrap(a, n);
}

int drive(int n) {                     // a caller: its local is written back
    int x[3] = {0, 0, 0};
    outer(x, n);
    zero_head(x, n);
    set_either(x, n);
    set_last(x, n);
    clamp_low(x, n);
    by_switch(x, n);
    bump_at(x, 2, 7);
    return x[0] + x[1] + x[2];
}

int ptr_form(int n) {
    int p[4] = {9, 9, 9, 9};
    via_ptr(p, n);
    return p[3] + p[0];
}

int rows(int n) {
    int g[2][2] = {{0, 0}, {0, 0}};
    fill_row(g, n);
    return g[0][0] + g[1][1];
}
