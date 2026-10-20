#include <stdio.h>

// Bare printf at file scope → `#eval` (main-spec §8.2). Exactly one specifier
// and nothing else: `#eval <expr>`; anything else: `IO.print` / `IO.println`.

int f(int x) { return x + 1; }

int loud(int x) {
    printf("loud %d\n", x);
    return x;
}

printf("%d", 2 + 3);
printf("%d", f(3));
printf("%s", "text");
printf("%c", 'q');
printf("x");
printf("x\n");
printf("v=%d", f(3));
printf("%d\n", f(3));
// comment above a script statement
printf("%x", 255);
fprintf(stderr, "e=%d", 1);
puts("p");
putchar('c');
printf("%d", loud(4));
