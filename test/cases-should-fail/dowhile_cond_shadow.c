// the `do … while` condition is re-tested at the end of the body, inside the
// scope of the local `j` — it would read the wrong one
int f(int n) {
    int j = 1;
    do {
        int j = 0;
        n += 1;
    } while (j > 0);
    return n;
}
