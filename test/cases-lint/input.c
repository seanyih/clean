#include <stdio.h>

// W015: scanf reads whole words and eats the whitespace after them.
// W016: a failing fopen stops the program (C: NULL).
// fgets, getchar and the stream output functions are quiet.
void quiet(void) {
    char line[32] = "";
    fgets(line, 32, stdin);
    int c = getchar();
    fputs("x\n", stdout);
}

void words(void) {
    unsigned n = 0;
    scanf("%u", &n);
    scanf("%u", &n);
}

void files(void) {
    FILE *f = fopen("a.txt", "r");
    FILE *g = fopen("b.txt", "w");
    fclose(f);
    fclose(g);
}
