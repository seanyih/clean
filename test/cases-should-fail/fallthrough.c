// a non-empty case that runs on into the next one is E105
int f(int x) {
    int r = 0;
    switch (x) {
    case 1: r = 1;
    case 2: r = 2; break;
    default: r = 3;
    }
    return r;
}
