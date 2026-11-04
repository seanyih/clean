#include "lean.h"

// an uninitialised local starts at zero (main-spec §4.5; lint W004 says so)
int pick(int x) {
    int r;
    if (x) r = 1; else r = 2;
    return r;
}

// every scalar type has its zero; a string starts empty and can be filled later
unsigned count(unsigned n) {
    unsigned c;
    for (unsigned i = 0; i < n; i++) c += 2;
    return c;
}
bool flag(void) { bool b; return b; }
double half(double x) { double d; d = x / 2.0; return d; }
bv(8) bits(void) { bv(8) v; return v; }
char nul(void) { char c; return c; }

// the common C buffer: `char buf[64];`, then filled by sprintf
int main(void) {
    char buf[64];
    int n;
    n = pick(0) + pick(5) + (int)count(3);
    sprintf(buf, "n=%d", n);
    puts(buf);
    return 0;
}
