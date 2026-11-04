// LEAN_EXPR in arithmetic: its type cannot be told
int f(int x) { return LEAN_EXPR("x") + 1; }
