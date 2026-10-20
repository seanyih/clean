#include <stdio.h>

// Console output → `IO` (main-spec §8.1, §3.3). A function that prints, or
// calls one that does, is `IO`; a call to it inside an expression is `(← f x)`.

int twice(int x) { return x * 2; }

void greet(int n) {
    printf("n = %d\n", n);
    printf("no newline");
    printf("%s and %c\n", "text", 'z');
    puts("a line");
    putchar('!');
    fprintf(stderr, "warn %d\n", n);
    fprintf(stderr, "warn");
    fprintf(stdout, "to stdout\n");
}

// printing from a branch, and a call to an IO function as a statement
int report(int a) {
    greet(a);
    int y = twice(a);
    if (y > 3) {
        printf("big: %d\n", y);
    } else {
        printf("small\n");
    }
    return y;
}

// the result of an IO function in an expression, in a condition, as an argument
int total(int a) {
    int r = report(a) + report(a + 1);
    if (report(0) == 0) {
        return twice(report(r));
    }
    return r;
}

// an IO function that writes an array parameter hands the array back (D20) and stays IO
void fill(int a[], int n) {
    a[0] = n;
    printf("filled\n");
}

void arrays(void) {
    int v[3] = {1, 2, 3};
    fill(v, 9);
    printf("%d\n", v[0]);
}

[[lean::partial]] void countdown(unsigned n) {
    printf("%u ", n);
    if (n > 0) countdown(n - 1);
}

// `main` is always IO; falling off the end returns 0 (C99).
int main(void) {
    int r = total(2);
    printf("r=%d\n", r);
    arrays();
    countdown(3);
}
