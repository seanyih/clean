// the same case value twice
int f(int x) {
    switch (x) {
    case 1: return 1;
    case 1: return 2;
    default: return 0;
    }
}
