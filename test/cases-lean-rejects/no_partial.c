// translated as written; Lean cannot prove that this terminates on `Int`, so it
// rejects it and the user adds [[lean::partial]] (main-spec §7.1)
int fact(int n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
}
