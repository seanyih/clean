// a bare block that hides an outer variable would change meaning in Lean
int f(int x) {
    int y = x + 1;
    {
        int y = 100;
        x = x + y;
    }
    return x + y;
}
