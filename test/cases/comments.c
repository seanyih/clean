/* comments.c — every place a comment can sit without breaking the C file.
   Both kinds survive the trip: the line kind becomes a Lean line comment,
   the block kind a Lean block comment. */
#include "lean.h"

/* above the first definition,
   spanning two lines */
int twice(int x) {
    /* directly above the local */
    int y = /* inside the initialiser */ x + x;
    return y;  // same line: belongs to whatever ends up after it
    /* the last comment of the body */
}

// a line comment above a definition works too
int half(int n) {
    if (n /* inside the condition */ > 0) {
        n = n - 1;
        /* end of the then-branch */
    }
    /* between the branches */
    else {
        n = 0;
        /* end of the else-branch */
    }
    switch (n) {
        /* before a label */
        case 0:
            /* first statement of the group */
            n = 1;
            /* above the break */
            break;
        /* between two groups */
        case 1:
            /* between label and body */
            n = 2;
            break;
        default:
            n = 3;
            /* above the closing break */
            break;
    }
    return n;
    /* the end of the body */
}

/* between two definitions */

/* this one mentions /- so it falls back to line comments
   (Lean block comments nest, so a stray opener would corrupt them) */

int f(int a) { return twice(a) + half(a); }

/* after the last definition, at end of file */
