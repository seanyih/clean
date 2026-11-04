#include "lean.h"

// raw Lean (main-spec §11.1): the escape hatch for what C cannot say.
// LEAN_DECL above every function lands before them (here: a Lean helper and an
// instance for a C struct need the struct first, so the struct comes first)
struct P { int x; int y; };
LEAN_DECL("def leanTriple (n : Int) : Int := 3 * n");
LEAN_DECL("instance : ToString P where\n  toString p := s!\"({p.x}, {p.y})\"");
extern int leanTriple(int n);

// LEAN_EXPR takes the type expected where it stands; a cast names it
int nine(void) { return leanTriple(3); }
int len(int a[], size_t n) { int k = LEAN_EXPR("a.size"); return k; }
bv(8) bits(void) { return (bv(8))LEAN_EXPR("0b1010#8"); }

// LEAN_STMT is one (or more) do-block lines, verbatim
int checked(int n) {
    int r = n * 2;
    LEAN_STMT("assert! r % 2 = 0");
    return r;
}

// LEAN_DECL below a function lands at the end, so it may talk about the functions
LEAN_DECL("theorem nine_eq : nine = 9 := by decide");
