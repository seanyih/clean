// loops land in a later slice
int f(int n) {
    int s = 0;
    while (n > 0) { s += n; n -= 1; }
    return s;
}
