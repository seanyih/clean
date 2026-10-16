#include "lean.h"

// integer addition
int add(int a, int b) { return a + b; }

// a call to another function
int twice(int x) { return add(x, x); }

/* a block comment above a definition also survives */
int triple(int x) { return add(x, twice(x)); }

// C and Lean precedence agree; parentheses are kept where they matter
int poly(int a, int b) { return a + b * 2 - (a - b) / 2; }

// unary minus
int neg(int x) { return x + -1; }

// hex literals keep their spelling
int mask(int x) { return x + 0xFF; }

int later(int x);

// a function without parameters
int zero(void) { return 0; }
