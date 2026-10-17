#include "lean.h"

// `double` → `Float`, `float` → `Float32`, `long double` → `Float`
// (main-spec §4.1). Floating literals keep their value; a trailing `f` is dropped.
double half(double x) { return x / 2.0; }

float scale(float x, float k) { return x * k + 1.5f; }

double poly(double x) {
    double r = 3 * x * x - 2.5 * x + 1e-3;
    return -r;
}

// float comparisons: `==`/`!=` are Lean's `==`/`!=` (no decidable `=` on floats)
int classify(double x) {
    if (x == 0.0) return 0;
    if (x < 0.0 || x >= 100.0) return 2;
    return 1;
}

bool near(double a, double b) { return a - b < 0.5 && b - a < 0.5; }

bool eq(double a, double b) { return a == b; }

int truthy(double x) {
    if (x) return 1;
    return 0;
}

long double wide(long double x) { return x; }

// casts (main-spec §5.3)
double fromInt(int x) { return (double)x; }
double fromNat(unsigned x) { return (double)x; }
float narrow(double x) { return (float)x; }
double widen(float x) { return (double)x; }
uint8_t toByte(double x) { return (uint8_t)x; }
