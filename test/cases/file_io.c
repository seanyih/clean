#include <stdio.h>

// File I/O (main-spec §8.1, §12.6). `FILE *` is a Lean `IO.FS.Stream`; the
// stream functions work on any of them, standard streams included. `fopen`
// takes "r", "w" or "a"; a file that cannot be opened stops the program with
// an error (clean has no NULL to compare with).

void write_numbers(FILE *out, int a, int b) {
    fprintf(out, "%d %d\n", a, b);
    fputs("text line\n", out);
    fputc('!', out);
    fputc('\n', out);
}

int sum_file(char path[]) {
    FILE *src = fopen(path, "r");
    int a = 0;
    int b = 0;
    fscanf(src, "%d %d", &a, &b);
    fclose(src);
    return a + b;
}

int main(void) {
    FILE *f = fopen("numbers.txt", "w");
    write_numbers(f, 7, 8);
    fclose(f);

    FILE *g = fopen("numbers.txt", "a");
    fprintf(g, "appended %s\n", "ok");
    fclose(g);

    printf("sum=%d\n", sum_file("numbers.txt"));

    FILE *h = fopen("numbers.txt", "r");
    char line[100] = "";
    fgets(line, 100, h);
    printf("first: %s", line);
    fgets(line, 100, h);
    printf("second: %s", line);
    int c = fgetc(h);
    printf("third starts with %c\n", c);
    fclose(h);

    FILE *out = stdout;
    FILE *err = stderr;
    fputs("through a variable\n", out);
    fprintf(err, "warning %d\n", 3);
    return 0;
}
