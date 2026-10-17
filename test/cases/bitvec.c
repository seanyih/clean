#include "lean.h"

#define W 8

// `bv(N)` → `BitVec N` (main-spec §4.1); `BV(N, v)` is the literal `v#N`.
// Widths are plain literals or `#define NAME <integer>`.
bv(8) sbox_xor(bv(8) a, bv(8) b) { return a ^ b; }

bv(W) mask(bv(W) x) { return x & BV(8, 0x0F); }

bv(4) nibble(bv(4) x) { return ~x; }

bv(16) wide(bv(16) a, bv(16) b) {
    bv(16) s = a + b;
    s <<= 1;
    s |= BV(16, 1);
    return s;
}

// plain literals take the vector's width
bv(8) bump(bv(8) x) { return x + 1; }

bv(8) neg(bv(8) x) { return -x; }

int isZero(bv(8) x) {
    if (x) return 0;
    return 1;
}

int lt(bv(8) a, bv(8) b) {
    if (a < b) return 1;
    if (a != b && a >= BV(8, 3)) return 2;
    return 0;
}

// casts to and from `bv`
bv(8) fromNat(unsigned x) { return (bv(8))x; }
unsigned toNat(bv(8) x) { return (unsigned)x; }
bv(8) fromU8(uint8_t x) { return (bv(8))x; }
