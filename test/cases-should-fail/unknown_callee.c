// calling something that is not defined in the file must be rejected here,
// not inside Lean (D5)
int f(int x) { return g(x) + 1; }
