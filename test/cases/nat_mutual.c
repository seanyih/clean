#include "lean.h"

// was cases-should-fail/recursion.c: a call cycle is translated as one
// `mutual … end` block — and with a base case on `Nat`, Lean proves
// termination, so no `[[lean::partial]]` is needed.
unsigned ping(unsigned n) {
    if (n == 0) return 0;
    return 1 + pong(n - 1);
}

unsigned pong(unsigned n) {
    if (n == 0) return 0;
    return 1 + ping(n - 1);
}
