// a function calling itself is a cycle of length one
int f(int n) { return f(n - 1); }
