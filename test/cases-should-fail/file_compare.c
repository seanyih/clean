// streams are compared by nothing
int f(void) { FILE *a = stdin; FILE *b = stdout; if (a == b) { return 1; } return 0; }
