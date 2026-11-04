// a static local keeps its value between calls: E106
int next(void) { static int n = 0; n = n + 1; return n; }
