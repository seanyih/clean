// A call of a user function at file scope (manual gap G17) is `#eval f x`, like
// `printf` in script mode (main-spec §8.2); an I/O argument needs `#eval do`.
#include <stdio.h>

void countdown(unsigned n) {
    while (n > 0) {
        printf("%d\n", n);
        n--;
    }
    puts("Blast off!");
}

unsigned three(void) {
    puts("computing three");
    return 3;
}

countdown(3);
countdown(three());
