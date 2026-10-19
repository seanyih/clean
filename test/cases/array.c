#include "lean.h"

#define N 4

// `T a[N] = {…}` → `#[…]`; a short list is zero-filled like C; `{0}` → `Array.replicate`;
// no initialiser → N default values; `a[]` takes its size from the list (main-spec §4.4).
int sum4(void) {
    int a[4] = {1, 2, 3, 4};
    return a[0] + a[1] + a[2] + a[3];
}
int zeros(void) {
    int z[N] = {0};
    return z[3];
}
int padded(void) {
    int p[5] = {7, 8};
    return p[4] + p[1];
}
int defaults(void) {
    int d[3];
    return d[0];
}
int inferred(void) {
    int q[] = {10, 20, 30};
    return q[2];
}

// element writes: `a[i] = v;` → `a := a.set! i v`; compound forms work too
int writes(void) {
    int a[3] = {1, 2, 3};
    a[1] = 20;
    a[2] += 5;
    a[0]++;
    return a[0] + a[1] + a[2];
}

// two dimensions: `Array (Array T)`, `a[i][j] = v` → `a.set! i (a[i]!.set! j v)`
int grid(void) {
    int g[2][3] = {{1, 2, 3}, {4, 5, 6}};
    g[1][2] = 60;
    return g[0][1] + g[1][2];
}
int grid0(void) {
    int g[2][2];
    return g[1][1];
}

// parameters: `T a[]`, `T *a` + a length, `T m[][N]`
int elemAt(int a[], int i) { return a[i]; }
int elemAtP(int *a, size_t n, int i) { return a[i] + n; }
int elemAt2(int m[][3], int i, int j) { return m[i][j]; }

// an `int` index is converted with `.toNat` (lint W009)
int idx(int a[], int i) { return a[elemAt(a, i)]; }

// element types
bool flags(void) {
    bool f[3] = {true, false, true};
    return f[2];
}
double ds(void) {
    double d[2] = {1.5, 2.5};
    return d[0] + d[1];
}
uint8_t bytes(void) {
    uint8_t b[3] = {250, 3};
    b[2] = b[0] + b[1];
    return b[2];
}

// an array passed on
int total(int a[], int n) { return a[0] + a[1] + n; }
int useTotal(void) {
    int v[2] = {3, 4};
    return total(v, 2);
}
