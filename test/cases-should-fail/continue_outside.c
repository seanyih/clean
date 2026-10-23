// `continue` binds to the innermost loop; there is none here
int f(int n) {
    if (n > 0) continue;
    return n;
}
