// the injected step (`i := i + k`) must still read the outer `k` at the end
// of the body and before the `continue` — the local `k` would shadow it
int f(int n) {
    int k = 1;
    for (int i = 0; i < n; i += k) {
        if (i % 2 == 0) {
            int k = 100;
            continue;
        }
        n += i;
    }
    return k;
}
