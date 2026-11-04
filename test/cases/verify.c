#include "lean.h"

// VERIFY_FORALL states a theorem over every value of its variables (main-spec §9.2, §9.3):
// the functions it reaches are unfolded, then `bv_decide` (a SAT search, checked by Lean) proves it
typedef bv(8) byte;

byte rotl1(byte x) { return (x << 1) | (x >> 7); }
byte rotl1_spec(byte x) { return bv_rotl(x, 1); }

// a function with locals is a `do` block: its unfolding needs `Id.run`, `pure`, `bind` as well
byte clamp_step(byte x) {
    byte y = x;
    if (y > BV(8, 3)) y = y - BV(8, 1);
    else y = y + BV(8, 1);
    return y;
}
byte clamp_spec(byte x) { return x > BV(8, 3) ? x - BV(8, 1) : x + BV(8, 1); }

uint8_t avg_floor(uint8_t a, uint8_t b) { return (a & b) + ((a ^ b) >> 1); }

VERIFY_FORALL(byte a) { rotl1(a) == rotl1_spec(a); }
VERIFY_FORALL(byte a) { clamp_step(a) == clamp_spec(a); }

// a label names the theorem; several variables; an `&&` chain proves every conjunct
avg_le_max: VERIFY_FORALL(uint8_t a, uint8_t b) { avg_floor(a, b) <= (a > b ? a : b) && avg_floor(a, a) == a; }
xor_self: VERIFY_FORALL(byte a, bool c) { (c ? a ^ a : BV(8, 0)) == BV(8, 0); }
