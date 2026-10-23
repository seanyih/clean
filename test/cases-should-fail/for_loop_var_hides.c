// the general `for` flattens its initialiser into the parent block, so the
// loop variable must not hide an outer name
int f(int n) {
    int i = 5;
    for (int i = 0; i < n; i++) n += i;
    return i;
}
