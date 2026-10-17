// break outside the end of a switch case
int f(int x) {
    int r = 0;
    switch (x) {
    case 1:
        if (r == 0) break;
        r = 5;
        break;
    default: r = 1;
    }
    return r;
}
