// a name that is not a parameter or a visible local
int f(int x) {
    {
        int inner = 1;
    }
    return x + inner;
}
