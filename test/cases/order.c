// callers may come before their callees; clean sorts them for Lean
int quad(int x) { return twice(twice(x)); }

int twice(int x);
int twice(int x) { return add(x, x); }

// a prototype can sit anywhere; only the definition order matters
int add(int a, int b) { return a + b; }

// already-ordered code keeps its place
int five(void) { return add(2, 3); }

// two callees, both below: they come out in the order they are called
int mix(int x) { return sub(x, 1) + inc(x); }
int inc(int x) { return x + 1; }
int sub(int a, int b) { return a - b; }
