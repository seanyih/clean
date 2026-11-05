// VERIFY_FORALL over functions whose bodies are `do` blocks with an early
// `return`: the `if`s on `UInt8` values are split before `bv_decide`.
#include "lean.h"

uint8_t maxA(uint8_t a, uint8_t b) { return a > b ? a : b; }

uint8_t maxB(uint8_t a, uint8_t b) {
    if (b > a) return b;
    return a;
}

uint8_t clampHigh(uint8_t x, uint8_t hi) {
    if (x > hi) return hi;
    return x;
}

maxEq: VERIFY_FORALL(uint8_t a, uint8_t b) { maxA(a, b) == maxB(a, b); }
clampBound: VERIFY_FORALL(uint8_t x, uint8_t hi) { clampHigh(x, hi) <= hi; }
