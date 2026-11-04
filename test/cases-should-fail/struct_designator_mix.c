struct P { int x; int y; };
int f(void) { struct P p = {.x = 1, 2}; return p.y; }
