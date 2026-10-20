#include <stdio.h>

// Format specifiers (main-spec §8.1): all become `s!` holes; `%x` goes through
// `hexOf` (or `toHex` on a `bv`); width, flags and precision are ignored (W006).

void ints(int a, unsigned u, uint8_t b, int64_t w, char c) {
    printf("%d %i %u %ld %lu\n", a, a, u, w, u);
    printf("%d\n", c);
    printf("%d, %d\n", 7, -7);
}

void hex(unsigned u, uint8_t b, bv(8) v, int a) {
    printf("%x %X\n", u, u);
    printf("%x %X\n", b, v);
    printf("%x\n", a);
    printf("%x\n", 255);
}

void floats(double d, float f) {
    printf("%f %g %e\n", d, d, f);
}

void padded(int a, unsigned u, double d) {
    printf("[%5d] [%-5d] [%08x] [%.2f]\n", a, a, u, d);
}

void text(char *s, char c) {
    printf("%s|%c|%%|{braces}|tab\there|\\n|quote\"\n", s, c);
    printf("");
    printf("\n");
    printf("two\n\n");
    printf("a" "b %d\n", 1);
}
