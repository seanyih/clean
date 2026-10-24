#include <stdio.h>
#include <stdlib.h>

// Nothing after `exit` can run.
int main(void) {
    exit(1);
    printf("never\n");
    return 0;
}
