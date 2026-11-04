#include "lean.h"

// W012 (shown whenever VERIFY_FORALL is used): more than 64 bits to search
VERIFY_FORALL(uint64_t a, bv(8) b) { (a ^ a) == 0 && (b & b) == b; }
