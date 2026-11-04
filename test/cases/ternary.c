#include "lean.h"

// `c ? a : b` is Lean's `if c then a else b` (main-spec §5.1)
int max2(int a, int b) { return a > b ? a : b; }

// right-associative: a chain of `?:` reads like an `else if` chain
int sign(int x) { return x > 0 ? 1 : x < 0 ? -1 : 0; }

// mixed branches meet at one type (D19): the `unsigned` one is converted to `int`
int pick(bool up, int a, unsigned b) { return up ? a : b; }

// inside arithmetic, as an argument, and as a value used as a test
int clampAdd(int a, int b) {
    int s = (a > 10 ? 10 : a) + b;
    int m = max2(s, b < 0 ? 0 - b : b);
    if (a ? b : m) m = m + 1;
    return m;
}

// a `bool`-valued branch, and a string result
bool between(int lo, int x, int hi) { return x < lo ? false : x <= hi; }
char *word(bool b) { return b ? "yes" : "no"; }

// on fixed widths and floats
uint8_t sat8(uint8_t a, uint8_t b) { return a > 255 - b ? 255 : a + b; }
double absd(double x) { return x < 0.0 ? -x : x; }
