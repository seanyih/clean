// there is no NULL: a failing fopen stops the program
void f(void) { FILE *f = fopen("x", "r"); if (f == NULL) { return; } fclose(f); }
