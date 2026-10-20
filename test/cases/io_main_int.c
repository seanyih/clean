#include <stdio.h>

int check(int n) {
    if (n > 2) {
        return 1;
    }
    return 0;
}

int main(void) {
    printf("%d\n", check(5));
    return check(1);
}
