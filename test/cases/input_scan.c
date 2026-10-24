#include <stdio.h>

// `scanf` (main-spec §8.1, §12.6): each conversion reads one whitespace-
// separated word into the variable it names. A function that reads is `IO`.
// A variable keeps its value when nothing could be read (as in C).

int sum_pair(void) {
    int a = 0;
    int b = 0;
    scanf("%d %d", &a, &b);
    return a + b;
}

unsigned count(void) {
    unsigned n = 5;
    scanf("%u", &n);
    return n;
}

int main(void) {
    int total = sum_pair();
    printf("total=%d\n", total);

    unsigned n = count();
    printf("n=%u\n", n);

    double d = 0.0;
    scanf("%lf", &d);
    if (d > 1.5) {
        printf("d is big\n");
    } else {
        printf("d is small\n");
    }

    char word[32] = "";
    scanf("%s", word);
    printf("word=%s\n", word);

    char c = '?';
    scanf(" %c", &c);
    printf("c=%c\n", c);

    int missing = 77;
    scanf("%d", &missing);
    printf("missing=%d\n", missing);
    return 0;
}
