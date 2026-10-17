#include "lean.h"

// `unsigned` → Lean `Nat` (main-spec §4.1): parameters, locals and return
// types all become `Nat`; literals keep their spelling.
unsigned twice(unsigned x) { return 2 * x; }

// `+ - * / %` keep their spelling with Lean's semantics (D4): on `Nat` they
// are total, and `-` truncates at 0 instead of wrapping (lint W002 someday).
unsigned mod_sum(unsigned a, unsigned b) {
    unsigned q = a / b + b % a;
    unsigned d = a - b;
    return q + d + twice(a);
}

// an assigned parameter becomes `let mut n : Nat := n`
unsigned bump(unsigned n) {
    n++;
    return n;
}
