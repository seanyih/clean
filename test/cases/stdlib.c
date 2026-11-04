#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

// <math.h> (main-spec §12.1): Float.*; an `int` argument converts (D19); the `…f` names are Float32
double hyp(double a, double b) { return sqrt(pow(a, 2.0) + pow(b, 2.0)); }
double rounded(int n) { return floor(sqrt(n)); }
float halfAngle(float x) { return atan2f(x, 1.0f) / 2.0f; }
bool bad(double x) { return isnan(x) || isinf(x); }

// <stdlib.h> abs, the min/max macro idiom, and the <limits.h>/<stdint.h> constants (§12.2)
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
int dist(int a, int b) { return abs(a - b); }
int clamp(int x, int lo, int hi) { return min(max(x, lo), hi); }
bool fitsInt8(int x) { return x >= INT8_MIN && x <= INT8_MAX; }
uint8_t full(void) { return UINT8_MAX; }

// <string.h> (§12.3): strlen, strcmp, strcat (a statement: the string is assigned back)
int cmp(char *a, char *b) { return strcmp(a, b); }
int greetLen(char *name) {
    char buf[32] = "hello, ";
    strcat(buf, name);
    return (int)strlen(buf);
}

// <ctype.h> (§12.4): Char's tests and case maps
int digits(char *s) {
    int n = 0;
    for (int i = 0; i < (int)strlen(s); i++)
        if (isdigit(s[i])) n++;
    return n;
}
char shout(char c) { return isalpha(c) ? toupper(c) : c; }

// <assert.h> (§12.5): `assert! c`, a run-time check
int safeDiv(int a, int b) {
    assert(b != 0);
    return a / b;
}

int main(void) {
    printf("%d %d %d %d\n", dist(3, 10), clamp(15, 0, 9), fitsInt8(200) ? 1 : 0, full());
    printf("%d %d %d\n", cmp("abc", "abd"), cmp("b", "a"), cmp("x", "x"));
    printf("%d %d %c%c\n", greetLen("bob"), digits("a1b22c333"), shout('q'), shout('7'));
    printf("%d\n", safeDiv(17, 5));
    return 0;
}
