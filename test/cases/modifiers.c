#include "lean.h"
#pragma lean namespace Geo.Util

// `#pragma lean namespace` wraps the file in `namespace … end` (main-spec §3.2);
// `static` is `private`, `inline` and `[[lean::inline]]` are `@[inline]` (§4.7)
static const int SIDES = 4;

static inline int sq(int x) { return x * x; }
[[lean::inline]] int perimeter(int side) { return SIDES * side; }
inline static int area(int side) { return sq(side); }

enum Shape { SQUARE, LINE };
int measure(enum Shape s, int n) {
    switch (s) {
    case SQUARE: return area(n) + perimeter(n);
    case LINE: return n;
    }
    return 0;
}
