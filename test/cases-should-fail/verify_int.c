// VERIFY_FORALL needs fixed widths: bv_decide cannot search an unbounded int
int sq(int x) { return x * x; }
VERIFY_FORALL(int a) { sq(a) >= 0; }
