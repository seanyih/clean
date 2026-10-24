// the count scanf returns is not translated: it is an IO action with no number to give back
int f(void) { int n = 0; int r = scanf("%d", &n); return r; }
