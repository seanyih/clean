#include <stdio.h>

// Loops and I/O together (main-spec §6, §8): write and read a file in `for`
// loops, and read standard input character by character until EOF. A loop
// that reads or prints makes its function `IO`; the loop itself is unchanged.

void write_squares(FILE *f, int n) {
    for (int i = 1; i <= n; i++) {
        fprintf(f, "%d\n", i * i);
    }
}

int sum_file(char path[], int n) {
    FILE *g = fopen(path, "r");
    int total = 0;
    int v = 0;
    for (int k = 0; k < n; k++) {
        fscanf(g, "%d", &v);
        total += v;
    }
    fclose(g);
    return total;
}

// the C idiom `while ((c = getchar()) != EOF)` is an assignment inside a
// condition (E103), so the read is written out
void count_input(void) {
    int lines = 0;
    int others = 0;
    int c = getchar();
    while (c != EOF) {
        if (c == '\n') {
            lines++;
        } else if (c == ' ') {
            c = getchar();
            continue;
        } else {
            others++;
        }
        c = getchar();
    }
    printf("lines=%d others=%d\n", lines, others);
}

int main(void) {
    FILE *f = fopen("squares.txt", "w");
    write_squares(f, 5);
    fclose(f);
    printf("total=%d\n", sum_file("squares.txt", 5));

    int tries = 0;
    do {
        tries++;
        printf("try %d\n", tries);
    } while (tries < 3);

    count_input();
    return 0;
}
