// E000: a /* that never closes is a broken comment, not a dropped one
int f(int x) {
    /* this never ends...
    return x;
}
