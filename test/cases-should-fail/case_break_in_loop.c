// inside a `switch` case, a `break` binds to the switch in C ("end the case
// early"), not to the enclosing loop — clean only translates the one that
// closes the case
int f(int n) {
    while (n > 0) {
        switch (n) {
            case 1:
                if (n > 2) break;
                n -= 1;
                break;
            default:
                n -= 2;
        }
    }
    return n;
}
