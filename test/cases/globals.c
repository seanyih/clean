#include <stdio.h>
#include <stdint.h>

// a `const` global is a Lean `def` (main-spec §7.3), and may use the ones above it
const int BASE = 21;
const int SCALE = BASE * 2;

// a constant table, a string, a struct value
const uint8_t SBOX[4] = {0x63, 0x7c, 0x77, 0x7b};
const char *GREETING = "hello";
struct Range { int lo; int hi; };
const struct Range DIGITS = {0, 9};

// constants are used like any value (C needs constant initialisers; Lean would also take a call)
const int AREA = SCALE * SCALE;

uint8_t sub(unsigned i) { return SBOX[i % 4]; }
int clampDigit(int d) { return d < DIGITS.lo ? DIGITS.lo : d > DIGITS.hi ? DIGITS.hi : d; }

// a local may shadow a constant inside a function body
int shadow(void) {
    int BASE = 1;
    return BASE + SCALE;
}

int main(void) {
    printf("%s %d %d %d\n", GREETING, SCALE, AREA, shadow());
    printf("%d %d %d\n", sub(5), clampDigit(12), clampDigit(-3));
    return 0;
}
