#include "lean.h"

// Every C spelling of the integer family maps through the type table in
// types.cpp (main-spec §4.1): signed spellings → `Int`, unsigned → `Nat`.
long widen(short a, signed b) { return a + b; }

unsigned long long big(unsigned short a, size_t b, unsigned long int c) {
    return a + b + c;
}

// `Int` and `Nat` mixed in one expression: Lean coerces (D4), the result is `Int`
long mixed(long a, unsigned b) {
    long r = a + b;
    if (b) return r;
    return 0;
}
