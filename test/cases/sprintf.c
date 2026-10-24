#include <stdio.h>

// `sprintf` / `snprintf` (main-spec §8.1 format table): the same formats as
// `printf`, but the text goes into a string variable instead of the console:
// `buf := s!"…"`. It is not I/O, so a function that only builds strings stays
// pure. `snprintf(buf, n, …)` keeps at most n-1 characters.

int scratch(int n) {
    char buf[16] = "";
    sprintf(buf, "%d", n);
    return n + 1;
}

int main(void) {
    char buf[64] = "";
    sprintf(buf, "n=%d hex=%x ch=%c s=%s\n", 255, 255, 'q', "str");
    printf("%s", buf);

    char small[64] = "";
    snprintf(small, 5, "abcdefgh%d", 12);
    printf("[%s]\n", small);

    int k = 4;
    snprintf(small, k + 4, "%d-%d-%d", 1, 2, scratch(k));
    printf("[%s]\n", small);
    return 0;
}
