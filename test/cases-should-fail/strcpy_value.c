// `strcpy` is a statement (`dst := src`); its result cannot be used.
#include <string.h>
int f(void) {
    char a[8] = "";
    char *b = strcpy(a, "x");
    return 0;
}
