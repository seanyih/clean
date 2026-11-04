// translated fine, but the claim is false: bv_decide finds a counterexample and Lean rejects it
#include "lean.h"
bv(8) dbl(bv(8) x) { return x + x; }
VERIFY_FORALL(bv(8) a) { dbl(a) == a << 2; }
