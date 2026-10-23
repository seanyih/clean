// C's comma operator in a `for` header is ⛔ (E103): the initialiser is one
// declaration, assignment or call
void f(int n) {
    int j = 0;
    for (j = 0, n = 5; j < n; j++) {
        n += 1;
    }
}
