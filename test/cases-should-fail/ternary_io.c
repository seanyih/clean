int ask(void) { printf("?\n"); return 1; }
int f(bool b) { int r = b ? ask() : 0; return r; }
