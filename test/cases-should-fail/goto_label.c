// a label exists only for `goto` (E102); even an unused one
int f(int n) {
    done: n = n + 1;
    return n;
}
