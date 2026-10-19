// a discarded non-void result does nothing in a pure language
int g(int x) { return x; }
int f(int x) { g(x); return x; }
