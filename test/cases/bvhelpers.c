#include "lean.h"
#define HI 7

// the bit-vector helpers of main-spec §5.4; `bv(N)` is signless, so signed
// readings have their own names
typedef bv(8) byte;

// slicing, concatenation, extension: the widths are part of the types
bv(4) high_nibble(byte x) { return bv_ext(x, HI, 4); }
bv(4) low_nibble(byte x) { return bv_ext(x, 3, 0); }
byte swap_nibbles(byte x) { return bv_cat(low_nibble(x), high_nibble(x)); }
bv(16) widen(byte x, bool sign) { return sign ? bv_sext(16, x) : bv_zext(16, x); }
bv(12) tag(byte x) { return bv_cat(x, BV(4, 0xF)); }

// single bits, rotations and the arithmetic shift
bool msb(byte x) { return bv_bit(x, 7); }
byte rotl3(byte x) { return bv_rotl(x, 3); }
byte rotr3(byte x) { return bv_rotr(x, 3); }
byte sar(byte x, unsigned k) { return bv_sshr(x, k); }

// signed and explicit unsigned comparisons; signed division
bool neg(byte x) { return bv_slt(x, BV(8, 0)); }
int order(byte a, byte b) {
    if (bv_slt(a, b)) return -1;
    if (bv_ule(a, b)) return 0;
    return 1;
}
byte sdiv2(byte x) { return bv_sdiv(x, BV(8, 2)); }

// a mismatch is E301; the helpers are the fix
bv(16) sum16(byte a, bv(16) b) { return bv_zext(16, a) + b; }
