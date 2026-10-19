// a `void` call has no value
void g(void) { return; }
int f(void) { int x = g(); return x; }
