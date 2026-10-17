// no uninitialised locals: Lean has none, and a made-up value would be a guess
int f(int x) {
    int r;
    if (x) r = 1; else r = 2;
    return r;
}
