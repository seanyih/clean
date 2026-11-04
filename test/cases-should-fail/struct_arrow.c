struct P { int x; };
int f(struct P p) { struct P *q = &p; return q->x; }
