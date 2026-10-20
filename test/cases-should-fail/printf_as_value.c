// printf returns an int in C; as an IO action it has no value to give back
int f(void) { int n = printf("x"); return n; }
