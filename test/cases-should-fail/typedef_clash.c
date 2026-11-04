// a type and a function cannot share a name in Lean
int number(void) { return 1; }
typedef int number;
