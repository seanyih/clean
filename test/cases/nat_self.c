#include "lean.h"

// was cases-should-fail/self_recursion.c: a function that calls itself is a
// cycle of length one — printed as a plain `def`, not a `mutual` block. With
// a base case on `Nat`, Lean proves termination: no `[[lean::partial]]`.
unsigned f(unsigned n) {
    if (n == 0) return 0;
    return 1 + f(n - 1);
}
