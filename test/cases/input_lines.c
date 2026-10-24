#include <stdio.h>

// `fgets` and `getchar` (main-spec §8.1): lines and single characters from
// standard input. `fgets(s, n, f)` reads at most n-1 characters of one line,
// the newline included, into the string `s`; at the end of the input `s` is
// left as it was (C returns NULL there). `getchar()` is an int: -1 (EOF) at the end.

void report(int c) {
    if (c == EOF) {
        printf("eof\n");
    } else {
        printf("char %c (%d)\n", c, c);
    }
}

int main(void) {
    char line[100] = "";
    fgets(line, 6, stdin);
    printf("[%s]\n", line);
    fgets(line, 100, stdin);
    printf("[%s]\n", line);

    report(getchar());
    fgets(line, 3, stdin);
    printf("[%s]\n", line);
    fgets(line, 100, stdin);
    printf("[%s]\n", line);

    report(getchar());
    report(getchar());
    fgets(line, 100, stdin);
    printf("[%s]\n", line);
    return 0;
}
