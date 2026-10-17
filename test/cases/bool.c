#include "lean.h"

// `bool` → `Bool`. Where a `bool` is wanted, a comparison becomes a `Bool`
// value (`decide`), `&& || !` become Lean's `&& || !`, and a number is
// converted by "non-zero is true" (D10).
bool less(int a, int b) { return a < b; }

bool inRange(int x, int lo, int hi) { return x >= lo && x <= hi; }

bool both(bool a, bool b) { return a && !b; }

bool nonzero(int x) { return x; }

bool yes(void) { return true; }

int pick(bool c, int a, int b) {
    if (c) return a;
    return b;
}

int clamp01(int x) {
    bool neg = x < 0;
    bool big = x > 1;
    if (neg) return 0;
    if (big) return 1;
    return x;
}

// a `bool` flows into tests, arguments and other bools
int viaCall(int x) {
    bool ok = inRange(x, 1, 9);
    if (ok && x != 5) return pick(ok, 1, 0);
    return pick(less(x, 0), -1, 0);
}

// `bool` equality
bool same(bool a, bool b) { return a == b; }
