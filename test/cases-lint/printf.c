#include <stdio.h>

// W006: width, flags and precision are ignored; `%f` is Lean's float text.
// Plain integer and string conversions are quiet.
void plain(int a, char *s) {
    printf("%d %s\n", a, s);
}

void padded(int a, double d) {
    printf("[%5d] [%.2f]\n", a, d);
    printf("%f\n", d);
}
