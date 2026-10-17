#include "lean.h"

// was cases-should-fail/statements_not_yet.c: locals and assignments are
// translated (`Id.run do`, `let mut`), and the recursion on `n / 10` is
// proven terminating by Lean on `Nat` — no `[[lean::partial]]`.
unsigned digit_sum(unsigned n) {
    unsigned s = 0;
    if (n == 0) return 0;
    s = n % 10;
    return s + digit_sum(n / 10);
}
