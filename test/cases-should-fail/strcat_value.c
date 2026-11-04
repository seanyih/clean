// strcat changes its first argument: a statement, not a value
int f(char *a) { char *b = strcat(a, "x"); return 0; }
