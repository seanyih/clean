#include "lean.h"

// Run with --lint=c-semantics: clean still translates with Lean's semantics
// (D4) and reports where C would behave differently. Each lint is reported
// once per kind per file: the first place, plus how many others there are.

// W001 (`int` is unbounded), W014 (`/` and `%` on a possibly negative int),
// W005 (the divisor may be zero)
int divide(int a, int b) { return a / b + a % 3; }

// W002: `Nat` subtraction stops at 0
unsigned gap(unsigned a, unsigned b) { return a - b; }

// W008: arithmetic on uint8_t is done in 8 bits
uint8_t add8(uint8_t a, uint8_t b) { return a + b; }

// W009: a signed value converted to unsigned clamps negatives to 0
unsigned clampNeg(int x) { return x; }

// W007: char <-> integer conversions go through Nat
unsigned code(char c) { return c + 1; }

// W010: a[i] panics out of range; W011: indexing a string is O(n)
int at(int a[], size_t i) { return a[i]; }
char nth(char *s, size_t i) { return s[i]; }
