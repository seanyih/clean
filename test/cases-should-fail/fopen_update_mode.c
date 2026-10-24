// update modes need a read-write handle: not built
void f(void) { FILE *f = fopen("x", "r+"); fclose(f); }
