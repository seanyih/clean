/* lean.h — extension macros for `clean` input; parse-only, never compiled.
 *
 * Normative content from spec/main-spec.md §3.1. The translator does not read
 * this file: including it only keeps C tooling able to parse the extensions.
 * Sparkle names (HW, HW_LAT, REG, NEXT, VERIFY_AT, …) are NOT here: see
 * main-spec §17 (`sparkle.h`, not built yet).
 */
#ifndef LEAN_H
#define LEAN_H

#define _BitVec(N)        unsigned _BitInt(N)   /* C23 stand-in so parsers accept it  */
#define bv(N)             _BitVec(N)            /* the short spelling users write     */
#define BV(N, v)          ((bv(N))(v))          /* width-explicit literal  -> v#N     */

#define LEAN_DECL(str)    /* raw Lean at file scope        */
#define LEAN_STMT(str)    /* raw Lean as a statement       */
#define LEAN_EXPR(str)    /* raw Lean as an expression     */

#define VERIFY_FORALL(...)         /* main-spec §9.2; one or more binders */
#define NOSYNTH                    /* function attribute: exempt from the synth profile */

#endif
