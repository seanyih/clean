// one variable per declaration: `int a, b;` is refused with a fix
int f(int x) { int a, b; a = x; b = a; return b; }
