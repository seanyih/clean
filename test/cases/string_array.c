// Arrays of strings (manual gap G19): `const char *a[N]` is an `Array String`
// as a constant, a local, and a parameter that a `void` function writes (D20).
#include <stdio.h>
#include <string.h>

const char *woodlandCritters[3] = {"hedgehog", "deer", "snail"};

const char *third(const char *xs[], unsigned n) {
    if (n > 2) return xs[2];
    return "";
}

void insertionSort(const char *arr[], unsigned n) {
    for (unsigned i = 1; i < n; i++) {
        unsigned j = i;
        while (j > 0 && strcmp(arr[j - 1], arr[j]) > 0) {
            const char *t = arr[j - 1];
            arr[j - 1] = arr[j];
            arr[j] = t;
            j--;
        }
    }
}

int main(void) {
    const char *rocks[4] = {"quartz", "marble", "granite", "hematite"};
    insertionSort(rocks, 4);
    for (unsigned i = 0; i < 4; i++) puts(rocks[i]);
    puts(third(woodlandCritters, 3));
    return 0;
}
