#include "lean.h"

// the caller is above the mutual pair; the pair is emitted together, callee-first overall
int parity_sum(int n) { return is_even(n) * 10 + is_odd(n); }

// `[[lean::partial]]` → `partial def`: Lean need not prove termination
[[lean::partial]] int fact(int n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
}

// mutual recursion → one `mutual … end` block (all members partial)
[[lean::partial]] int is_even(int n) {
    if (n == 0) return 1;
    return is_odd(n - 1);
}
[[lean::partial]] int is_odd(int n) {
    if (n == 0) return 0;
    return is_even(n - 1);
}

// a recursive function without the attribute is translated as it is;
// Lean judges it (see test/cases-lean-rejects). This one is fine: no recursion.
int not_recursive(int n) { return fact(n) + 1; }
