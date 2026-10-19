#include "lean.h"

// Decision D19: a mix of types is converted, the conversion chosen from the
// types as written (main-spec §5.3); C's usual arithmetic conversions are
// NOT replicated. The higher rank wins:
//   unsigned < int < intN_t/uintN_t < bv(N) < float < double
// A literal takes the other side's type.
long addMixed(long a, unsigned b) { return a + b; }

// int → uint8_t wraps (UIntN.ofNat of the value mod 2^8)
uint8_t narrowMix(uint8_t a, int b) { return a + b; }

// two fixed widths: the wider one wins
uint32_t wideWins(uint8_t a, uint32_t b) { return a + b; }

// `char` is its code in arithmetic (§4.2); two chars compared stay `Char`
unsigned digit(char c) { return c - '0'; }
char next(char c) { return c + 1; }
bool isA(char c) { return c == 65; }
bool sameChar(char a, char b) { return a == b; }

double scaled(int x) { return x * 1.5; }
double widen(float f, double d) { return f + d; }
float keepsWidth(float f) { return f * 2; }

bv(8) bvMix(bv(8) a, unsigned n) { return a + n; }

// an implicit `int` → `unsigned` clamps negatives to 0 (lint W009)
unsigned clampNeg(int x) { return x; }

// the result of a shift has the left operand's type; the count converts to it
uint16_t shiftBy(uint16_t a, int n) { return a << n; }

// arguments convert to the parameter types too
uint8_t byteOf(uint8_t x) { return x; }
uint8_t callMixed(int x) { return byteOf(x); }

// number → bool is "non-zero" (D10)
bool anyBits(uint8_t x) { return x; }
