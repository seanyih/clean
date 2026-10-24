#include <stdio.h>
#include <stdlib.h>

// `exit(n)` → `IO.Process.exit` (main-spec §8.1). It ends the program, so like
// `return` it ends its block: a branch that exits needs no `return` after it.
// The status is taken mod 256, as in C.

int half(int n) {
    if (n < 0) {
        printf("negative\n");
        exit(2);
    } else {
        return n / 2;
    }
}

int positive(int n) {
    if (n <= 0) exit(EXIT_FAILURE);
    return n;
}

void stop(int code) {
    printf("stopping with %d\n", code);
    exit(code);
}

int main(void) {
    printf("%d\n", half(8));
    printf("%d\n", positive(5));
    stop(263);
    printf("never printed\n");
    return 0;
}
