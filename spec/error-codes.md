# Error codes — catalogue with implementation status

Extracted from main-spec §13.2. The severity, trigger and fix hint stay
normative here; the **Implemented?** column is fact gathered from `src/`
(state: slices 0–6, 2026-10-18). Update that column whenever a slice lands —
it is the at-a-glance view of what is left to build.

Messages render exactly as main-spec §13.1 prescribes (implemented in
`src/diag.h` `render()`):

```
<code>  file.c:LINE:COL  <what>
      why:  <one sentence>
      fix:  <concrete rewrite>
```

Errors (E) and hints (H) are always shown. Lints (W) are off by default and
come with `--lint=c-semantics` (W001–W011 and W014); W012/W013 come with the
features that trigger them. Each lint is reported once per kind per file: the
first place, plus a count of the others. A lint never changes the translation
or the exit code (decision D18; `src/lint.cpp`). Not built because their
feature does not exist yet: W003 (`long double`), W004 (uninitialised locals
are still refused), W006 (`printf`), W012, W013.

**E000 — the build's placeholder, not a catalogue code.** Every refusal that
has no permanent number yet is reported as E000. Two flavours:

- *not translated in this slice* — `struct`, `enum`, `typedef`, `while`,
  `goto`, … (the `kRefused` list in types.cpp and `kLaterStatements` in
  parser.cpp). These retire as their slices land.
- *permanent syntax/scope refusals* that will never arrive with a feature:
  unknown name or callee, arity mismatch, missing return, unreachable code,
  duplicate declaration/case/default, bare-block shadowing,
  comparison-as-value, stray `break`, attribute on a prototype, `partial` on
  only part of a `mutual` group (parser.cpp, check.cpp, toposort.cpp).

Whether the second flavour should get its own numbers is open decision OD10
in [open-decisions.md](open-decisions.md).

Status legend: ✅ emitted today (file that throws it) · ⚠️ partial ·
❌ not built — see the note for what happens today instead.

## Catalogue

| Code | Severity      | Trigger                                                                                              | Fix hint                                                                   | Implemented? (slices 0–6)                                                                          |
| ---- | ------------- | ---------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------- |
| E101 | error         | pointer use outside §4.6                                                                             | use array parameter or return value                                        | ⚠️ pointers outside §4.6 ✅ (types.cpp `applyDeclarator`; `T *a` needs a length parameter, parser.cpp); mutation of a string by index ✅ (check.cpp); pointer arithmetic, `T **` and `void *` are refused here too. Writes to an array parameter are an E000 refusal, see OD11 |
| E102 | error         | `goto` / unused label                                                                                | restructure with loops                                                     | ❌ `goto` lands in the E000 "not translated in this slice" refusal (parser.cpp `kLaterStatements`)  |
| E103 | error         | assignment as expression, comma operator, unrecognised verification loop                             | split into statements                                                      | ⚠️ assignment-as-expression ✅ parser.cpp `parseExpr`; comma operator and verification loops not built |
| E104 | error         | `union`                                                                                              | use `bv(N)` and `bv_ext`/`bv_cat`                                           | ❌ E000 type refusal (types.cpp `kRefused`)                                                          |
| E105 | error         | `switch` fall-through                                                                                | add `break` or merge cases                                                 | ✅ check.cpp `switchStmt`                                                                            |
| E106 | error         | mutable global or `static` local                                                                     | pass state as parameter                                                    | ❌ file scope only takes functions; `static` is an E000 type refusal                                  |
| E107 | error         | function pointer (v0.1)                                                                              | pass a named function (🟡)                                                 | ❌ syntax E000                                                                                        |
| E108 | error         | `malloc`/`free`                                                                                      | fixed-size array                                                          | ❌ surfaces as the unknown-callee E000 in check.cpp                                                   |
| E109 | error         | `volatile`, inline asm, variadics (except `printf` family), `setjmp`                                 | remove                                                                     | ❌ `volatile` is an E000 type refusal (types.cpp `kRefused`); asm/variadics are syntax E000                                  |
| E110 | error         | `++`/`--` inside an expression                                                                        | separate statement                                                        | ✅ parser.cpp `parseUnary` (`failIncDec`)                                                             |
| E111 | error         | bit-field                                                                                            | `bv(N)` member                                                             | ❌ syntax E000                                                                                        |
| E112 | error         | `sizeof`                                                                                             | pass the size explicitly                                                   | ❌ surfaces as the unknown-name E000 in check.cpp                                                     |
| E113 | error         | threads/atomics                                                                                      | —                                                                          | ❌ not built                                                                                          |
| E201 | error (synth) | recursion                                                                                            | unroll or bound                                                            | ❌ no `synth` profile exists; recursion is translated and Lean judges termination                      |
| E202 | error (synth) | loop not reducible to range-for                                                                     | constant-bound `for`                                                       | ❌ no loops yet (`for`/`while` are E000 refusals)                                                     |
| E203 | error (synth) | float types                                                                                          | fixed-point with `bv(N)`                                                   | ❌ no `synth` profile (float types are translated now) |
| E204 | error (synth) | `String`/`char *`                                                                                    | `bv(8)` arrays                                                             | ❌ no `synth` profile; no strings                                                                    |
| E205 | error (synth) | I/O (`printf`, …)                                                                                    | mark function `NOSYNTH`                                                   | ❌ no `synth` profile; no I/O translation                                                             |
| E208 | error (synth) | unbounded `int`/`unsigned`                                                                           | `intN_t`/`uintN_t`/`bv(N)`                                                 | ❌ no `synth` profile                                                                                  |
| E301 | error         | bit-width mismatch                                                                                   | `bv_zext`, `bv_sext`, `bv_ext`                                             | ✅ types.cpp `unify` / check.cpp `valueAs` (`bv(8)` with `bv(4)`); the §5.4 helpers it suggests are not built |
| E302 | error         | non-constant `bv` width                                                                              | use a constant or `#define`                                                | ⚠️ parser.cpp `parseWidthValue`: literal or `#define NAME <integer>` ✅; other constant expressions are refused with this code |
| E303 | error         | bitwise operator on `Int`                                                                            | `uintN_t` or `bv(N)`                                                       | ✅ types.cpp `checkBinaryOp` / `checkUnaryOp`                                                         |
| E304 | error         | value-level call of a `HW` extern whose latency is unknown, or of a stateful module with no `HW_LAT` | declare `HW` or `HW_LAT(n)`; stateful modules are checked with `VERIFY_AT` | ❌ no Sparkle support yet                                                                             |
| E401 | error         | Sparkle name (`HW`, `REG`, `NEXT`, …) used without `#include <sparkle.h>`                           | add the include (§17.2)                                                    | ❌ no Sparkle support yet                                                                             |
| E402 | error         | `REG`/`NEXT` outside an `HW` function body                                                           | move into an `HW` body                                                     | ❌ no Sparkle support yet                                                                             |
| E403 | error         | `NEXT` target is not a `REG`, or the same register has two `NEXT`s                                   | one `NEXT` per register; use `?:` for conditions                          | ❌ no Sparkle support yet                                                                             |
| E404 | error         | `HW` body calls a non-`HW` function                                                                  | mark the callee `HW`, or inline it                                         | ❌ no Sparkle support yet                                                                             |
| E405 | error         | signal-valued local reassigned in a way that is not a plain conditional                              | use `?:` or one assignment per branch                                     | ❌ no Sparkle support yet                                                                             |
| W001 | lint          | `int`-family is unbounded `Int`                                                                      | `intN_t` for fixed width                                                   | ✅ lint.cpp (`--lint=c-semantics`) |
| W002 | lint          | `Nat` subtraction truncates at 0                                                                     | `--unsigned=int` or guard                                                  | ✅ lint.cpp |
| W003 | lint          | `long double` → `Float`                                                                              | —                                                                          | ❌ no lint pass (`long double` → `Float` is translated silently) |
| W004 | lint          | uninitialised variable zero-initialised                                                              | initialise                                                                 | ❌ today `int x;` is refused outright (E000, "picking a value would be guessing"); zero-init + W004 arrives with that slice |
| W005 | lint          | division by zero is `0`                                                                              | guard                                                                      | ✅ lint.cpp |
| W006 | lint          | `printf` width/precision ignored                                                                     | —                                                                          | ❌ no `printf` translation yet                                                                        |
| W007 | lint          | `char` ↔ integer conversion                                                                           | `uint8_t`                                                                  | ✅ lint.cpp (casts between `char` and integers, inserted or written) |
| W008 | lint          | exact-width arithmetic, no `int` promotion                                                           | —                                                                          | ✅ lint.cpp (`intN_t`/`uintN_t` narrower than 32 bits) |
| W009 | lint          | `Int.toNat` clamps negative values                                                                    | —                                                                          | ✅ lint.cpp (`Int`/`intN_t` → `Nat`/`char`, inserted or written; also an `int` used as an index) |
| W010 | lint          | `a[i]!` can panic                                                                                     | `--index=proof`                                                            | ✅ lint.cpp (`a[i]` reads, and element writes: `set!` ignores a bad index) |
| W011 | lint          | string indexing is O(n)                                                                               | `Array Char`                                                               | ✅ lint.cpp |
| W012 | warning       | verification domain > 64 bits                                                                        | split                                                                      | ❌ no verification constructs yet                                                                    |
| W013 | warning       | `sorry` emitted by `--prove=none`                                                                    | —                                                                          | ❌ no `--prove` flag exists                                                                           |
| W014 | lint          | `/` or `%` on `Int` rounds differently from C when an operand is negative (Euclidean vs truncating) — added with D18 | non-negative operands, or `unsigned`; C's truncating division is not emitted | ✅ lint.cpp |
| H001 | hint          | `bool a[N]`                                                                                          | `bv(N) a;`                                                                 | ✅ check.cpp `declStmt`; silenced by `#pragma lean allow_bool_array` |
| H002 | hint          | loop is not full-domain                                                                              | `VERIFY_FORALL`                                                            | ❌ no verification loops yet                                                                          |
| H003 | hint          | `unsigned` where exact width intended                                                                 | `uintN_t`                                                                  | ❌ no hint pass (fixed-width types are translated) |
| H004 | hint          | bitwise on `char`                                                                                     | `uint8_t`                                                                  | ✅ refused with this code: types.cpp `checkBinaryOp` / `checkUnaryOp` |
