// `fflush(f)` is `f.flush` (like `fclose`), and `strcpy(dst, src);` is the
// statement `dst := src` (manual gaps G25 and the `line = ""` workaround).
#include <stdio.h>
#include <string.h>

int main(void) {
    char line[64] = "unchanged";
    unsigned count = 0;
    fputs("Lines? ", stdout);
    fflush(stdout);
    strcpy(line, "");
    fgets(line, 64, stdin);
    while (strcmp(line, "") != 0) {
        count++;
        strcpy(line, "");
        fgets(line, 64, stdin);
    }
    printf("%d\n", count);
    return 0;
}
