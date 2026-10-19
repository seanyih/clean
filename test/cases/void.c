#include "lean.h"

// `void` → `Unit` (main-spec §4.1). Functions are pure, so a `void` function
// can do nothing observable until `printf` arrives (§8); it translates, and
// calling it as a statement is accepted.
void nothing(void) { return; }

void note(int x) {
    int y = x + 1;
    if (y > 3) return;
}

void empty(int a, int b) {}

int caller(int x) {
    note(x);
    nothing();
    empty(x, 2);
    return x;
}
