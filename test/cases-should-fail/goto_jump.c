// main-spec §6: `goto` is ⛔ (E102)
int f(int n) {
    if (n > 0) goto done;
    return 0;
done:
    return 1;
}
