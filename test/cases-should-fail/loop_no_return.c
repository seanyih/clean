// a loop is not a return path: clean cannot prove the loop never falls
// through, so the function needs a trailing return
int f(int n) {
    while (n > 0) {
        if (n == 2) return 1;
        n -= 1;
    }
}
