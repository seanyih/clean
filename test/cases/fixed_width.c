#include "lean.h"

// Exact-width types map to Lean's own wrapping types (main-spec §4.1):
// intN_t → IntN, uintN_t and unsigned char → UIntN. Nothing is emulated.
uint8_t add8(uint8_t a, uint8_t b) { return a + b; }

int32_t mix32(int32_t a, int32_t b) {
    int32_t t = a * 3 - b;
    t += 7;
    return t;
}

unsigned char byte(unsigned char x) { return x; }

// bitwise operators exist on the fixed-width types (and on bv and unsigned)
uint16_t pack(uint8_t hi, uint8_t lo) {
    uint16_t h = (uint16_t)hi;
    uint16_t l = (uint16_t)lo;
    return (h << 8) | l;
}

// Lean's operator levels differ from C's: the tree is printed with the
// parentheses Lean needs, so `a << (b + 1)` stays what C meant.
uint8_t shifty(uint8_t a, uint8_t b) { return a << (b + 1); }

uint32_t flags(uint32_t x, uint32_t m) { return (x & m) ^ ~x; }

// casts between the integer types (main-spec §5.3)
unsigned widen(uint8_t x) { return (unsigned)x; }
uint8_t narrow(unsigned x) { return (uint8_t)x; }
int signedOf(int8_t x) { return (int)x; }
int8_t wrap8(int x) { return (int8_t)x; }
uint8_t fromInt(int x) { return (uint8_t)x; }

int cmp8(uint8_t a, uint8_t b) {
    if (a < b) return 1;
    if (a == b) return 0;
    return 2;
}

int truth8(uint8_t a) {
    if (a) return 1;
    return 0;
}
