// C cannot copy an array with =
int f(void) { int a[2] = {1, 2}; int b[2] = a; return b[0]; }
