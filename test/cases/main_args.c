#include <stdio.h>

// `main(int argc, char *argv[])` (main-spec §7.1): Lean's `main` gets the
// arguments as one `List String`; `argc` and `argv` are rebuilt from it, with
// the program's own name as argv[0] (its value is Lean's, not C's: do not
// print it). `argv` is the one array of strings clean knows.

void greet(char name[]) {
    printf("hello %s\n", name);
}

int main(int argc, char *argv[]) {
    printf("argc=%d\n", argc);
    if (argc > 1) {
        greet(argv[1]);
    }
    if (argc > 2) {
        printf("last=%s\n", argv[argc - 1]);
    }
    return argc;
}
