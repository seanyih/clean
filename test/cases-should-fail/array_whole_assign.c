// C rejects assigning an array
int f(void) {
    int a[2] = {1, 2};
    int b[2] = {3, 4};
    a = b;
    return a[0];
}
