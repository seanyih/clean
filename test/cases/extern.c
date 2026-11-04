#include "lean.h"

// `extern` means "Lean already defines it" (main-spec §7.2.1): nothing is emitted for it,
// calls use it as is. With --check-externs, an `example` makes Lean confirm the type.
typedef bv(8) byte;
extern byte sbox_spec(byte);
extern int gcd_spec(int a, int b);
extern const int LIMIT;

// C written against the Lean reference
byte sbox_twice(byte x) { return sbox_spec(sbox_spec(x)); }
int bounded_gcd(int a, int b) { return a > LIMIT ? 0 : gcd_spec(a, b); }
bool agrees(byte x, byte y) { return sbox_spec(x ^ y) == sbox_spec(y ^ x); }
