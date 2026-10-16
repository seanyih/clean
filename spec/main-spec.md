# C2Lean — C-to-Lean Language Mapping Specification

**Version:** 0.3 (draft) · **Target:** Lean 4.35.0 · **Date:** 2026-10-16
**Status:** specification only — no implementation is implied or included.

### Revision notes (v0.2 → v0.3)

1. **Q1 resolved:** exact-width types (`int8_t`, `uint8_t`, …) map to Lean's own wrapping types (§4).
2. **Dedicated Sparkle header (your proposal).** Sparkle-specific syntax moves out of `lean.h` into `sparkle.h`; `lean.h` becomes Sparkle-free ("vanilla Lean"). Feasibility findings, layering rules, the header's function set, and the semantics of C-authored hardware modules are in the new **§17**. `HW`, `HW_LAT`, `VERIFY_AT` moved to `sparkle.h`; D14 is revised and D15–D17 are added.
3. The v0.2 non-goal "never generates Sparkle code" is narrowed: it holds for `lean.h`; with `sparkle.h`, `HW` function bodies in a restricted subset may be translated (🟡, §17).

### Revision notes (v0.1 → v0.2)

1. **Syntax-sugar stance (your Q1/Q2).** `int` → `Int`, `unsigned` → `Nat`. The tool is *C spelling with Lean semantics*; it does not emulate C overflow, promotion, truncating division or undefined behaviour. The earlier W-warnings become opt-in lints (D4).
2. **Sparkle integration (your Q3).** Hardware modules written in Sparkle are `Signal dom α` streams, not values. v0.2 adds *HW externs*, a lift rule, and equivalence theorems stated with `atTime`, aligned with Sparkle's own `#verify_eq_at` (§7.2, §9). Sparkle's source was read to confirm the definitions used.
3. Bit-vector slicing now uses `extractLsb'`, the form Sparkle itself uses (§5.4).

---

## 0. Reading guide

### 0.1 Status tags

| Tag | Meaning                                                                                                       |
| --- | ------------------------------------------------------------------------------------------------------------- |
| ✅   | In scope for v0.1 release (priority 1: *Functional Programming in Lean* chapters 1–2, IO, arrays, structures) |
| 🟡  | Planned for v0.2–v0.3                                                                                         |
| 🔮  | Future / research                                                                                             |
| ⚠️  | Accepted; an opt-in lint may report it                                                                        |
| ⛔   | Rejected with an error and a fix hint                                                                         |

### 0.2 Profiles and the Synth column

The tool has two profiles (decision D6):

- **`model`** (default): anything translatable is accepted. Use it for ground-truth reference models.
- **`synth`**: additionally rejects constructs a hardware flow cannot synthesize. Use it for code that stands in for hardware.

In tables, the **Synth** column says whether a construct is allowed in the `synth` profile: **S** = allowed, **N** = model-only (rejected in `synth`).

### 0.3 Sources

1. *Functional Programming in Lean* (FPIL), https://lean-lang.org/functional_programming_in_lean/ — priority 1. Its front page states that code samples are tested with Lean 4.32.0, so small drift against 4.35 is possible.
2. *The Lean Language Reference*, https://lean-lang.org/doc/reference/latest/ — covers Lean 4.35.0.
3. Sparkle HDL (Verilean/sparkle): `README.md`, `Sparkle/Core/Signal.lean`, `docs/reference/Verification_Framework.md`, `docs/known-issues/KnownIssues.md`. Used for §7.2 and §9. For §17 the Reservoir package page (https://reservoir.lean-lang.org/@Verilean/sparkle, read 2026-10-16) was also used: imports, `Signal.circuit` example, command list, toolchain note.

Every API name that could not be confirmed is collected in **Appendix B**; check those before implementing.

---

## 1. Purpose, audience, non-goals

### 1.1 Purpose

Let IC designers write **ground-truth models and equivalence checks in C-like syntax** and obtain **idiomatic Lean 4** that can be type-checked, evaluated, and (for the narrow equivalence pattern in §9) proved. Lean remains the proof layer; C is a more familiar *authoring surface* for its functional-programming part.

C acts as a pure syntax sugar instead of a working model. For example, `int` has overflow in C, but we translate it directly to LEAN's `Int` . Also, we don't strictly ask if the C code can really run or identical to the result of LEAN.

### 1.2 Audience

C-fluent hardware engineers who do not want to learn Lean syntax. The tool must be **permissive on input and precise in diagnostics** (D10).

### 1.3 Non-goals

- The input C does **not** have to compile with a C compiler. It must **parse** (§3.1).
- **The tool is syntax sugar, not a C-behaviour translator.** It maps C spellings to Lean types and operators and inherits Lean's semantics (unbounded `Int`, truncating `Nat` subtraction, total division, …). Bit-exact C behaviour is not a goal.
- No general proof synthesis. Only the equivalence pattern in §9 produces theorems.
- **Vanilla C (`lean.h` only) never produces Sparkle Signal-DSL code.** Its functions are pure value-level specs. Only files that include `sparkle.h` may define `HW` modules, and only in a narrow subset (§17); Sparkle's synthesis elaborator accepts specific patterns only, so this stays a restricted, 🟡 feature.
- C++ is out of scope for v0.1 (§15). The reverse direction (Lean to C) is out of scope.

---

## 2. Design decisions

| ID  | Decision                                                                                                                                                                                                                                                                         | Rationale                                                                                                                             |
| --- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| D1  | **One-way, C → Lean.** Not every Lean construct has a C spelling. Anything unreachable is covered by explicit passthrough macros (`LEAN_DECL`, `LEAN_STMT`, `LEAN_EXPR`).                                                                                                        | "All Lean syntax has a C counterpart" cannot hold (dependent types, tactics, universe levels). Passthrough keeps the tool open-ended. |
| D2  | C need not compile, but must parse. A prelude header (`lean.h`, §3.1) supplies `bv`, `BV`, `VERIFY_FORALL`, `LEAN_*`, `NOSYNTH`; Sparkle names come from `sparkle.h` (§17).                                                                                                      | Lets IDEs and a C front-end parse hardware-flavoured C without a build.                                                               |
| D3  | Output is **idiomatic Lean**: `def`, `do`, `let mut`, `for`, `match`. It is not an AST plus interpreter.                                                                                                                                                                         | Readable for review; matches what FPIL teaches.                                                                                       |
| D4  | **C syntax, Lean semantics.** The tool does not emulate C. Divergences from C behaviour are reported only by **opt-in lints** (`--lint=c-semantics`, codes W001–W011); they are off by default. Genuinely unsupported syntax is still an **error**.                              | Your stated intent: pure syntax sugar. Keeps normal output quiet while allowing a C-minded reviewer to audit.                         |
| D5  | **Reject, never guess.** Unsupported constructs are errors with a fix hint. No silent `sorry`/`axiom`.                                                                                                                                                                           | The translator is in the trusted base; silent wrong answers are worse than refusals.                                                  |
| D6  | Two profiles: `model` and `synth` (§0.2).                                                                                                                                                                                                                                        | Ground-truth models may use strings, floats, `printf`; hardware stand-ins may not.                                                    |
| D7  | **Purity is inferred**: pure → plain `def`; local mutation → `Id.run do`; I/O → `IO`.                                                                                                                                                                                            | Keeps pure functions pure, so they can appear in theorems.                                                                            |
| D8  | Identifiers are preserved verbatim; Lean keywords fails loudly.                                                                                                                                                                                                                  | Predictable names.                                                                                                                    |
| D9  | Every emitted declaration carries a traceability comment `-- c: file.c:LINE`.                                                                                                                                                                                                    | Review and debugging.                                                                                                                 |
| D10 | **Permissive input, strict diagnostics**: missing includes/prototypes, int-as-bool, implicit conversions are accepted; the tool inserts the explicit Lean form and explains on request.                                                                                          | "Friendly for C users to write."                                                                                                      |
| D11 | Theorems are generated only for the narrow equivalence pattern (§9).                                                                                                                                                                                                             | Scope control.                                                                                                                        |
| D12 | The translator is a trusted component. A differential-testing harness (run C-side model vs `#eval` of the output) is recommended but non-normative.                                                                                                                              | Mitigates translator bugs that Lean cannot catch.                                                                                     |
| D13 | v0.1 maps **all C arrays to `Array`**. `Vector α n` is opt-in (🟡).                                                                                                                                                                                                              | Matches FPIL, avoids index-proof friction.                                                                                            |
| D14 | **Hardware modules are `HW` functions (from `sparkle.h`).** From vanilla C they are called with plain `BitVec`/`Bool` values; the tool lifts a call into Sparkle's `Signal` world by driving constant streams and sampling at a declared latency (§7.2, §17.5).                  | `Signal dom α` is a time stream (`Nat → α`); C has no time. A fixed, explicit convention keeps the C side time-free.                  |
| D15 | **Vanilla Lean and Sparkle Lean are separated by header.** `lean.h` knows nothing of Sparkle; `sparkle.h` includes `lean.h` and adds the Sparkle names under the `[[sparkle::…]]` attribute namespace. Output imports Sparkle only when the header is included and used (§17.2). | Contains Sparkle API churn to one header plus one emitter back-end; vanilla output never depends on Sparkle.                          |
| D16 | **One attribute, `extern` decides where the module lives.** `HW extern T f(…);` = defined in Lean; `HW T f(…) { … }` = defined in C and translated (🟡).                                                                                                                         | One concept for the user; same call rule either way.                                                                                  |
| D17 | **`HW` bodies always obey the `synth` subset**, whatever the file's profile (§17.4).                                                                                                                                                                                             | A hardware body that is not synthesizable is a bug, not a model.                                                                      |

---

## 3. Input and output model

### 3.1 Input: C plus a prelude header

The user includes (or the tool implicitly injects) `lean.h`. It exists only so a C parser can digest the extensions. Its content is normative:

```c
/* lean.h — extension macros; parse-only, never compiled to a binary */
#define _BitVec(N)        unsigned _BitInt(N)   /* C23 stand-in so parsers accept it  */
#define bv(N)             _BitVec(N)            /* the short spelling users write     */
#define BV(N, v)          ((bv(N))(v))          /* width-explicit literal  -> v#N     */

#define LEAN_DECL(str)    /* raw Lean at file scope        */
#define LEAN_STMT(str)    /* raw Lean as a statement       */
#define LEAN_EXPR(str)    /* raw Lean as an expression     */

#define VERIFY_FORALL(...)         /* §9.2; one or more binders */
#define NOSYNTH                    /* function attribute: exempt from the synth profile */

/* Sparkle names (HW, HW_LAT, REG, NEXT, VERIFY_AT, …) are NOT here: see sparkle.h, §17 */
```

C23 attribute spellings are also accepted: `[[lean::partial]]`, `[[lean::inline]]`, `[[lean::nosynth]]`, `[[lean::name("x")]]`.

The standard preprocessor runs first. Object-like numeric macros used as `bv(...)` widths or array sizes are intended to stay symbolic (🟡, §7.4); in v0.1 they are expanded.

### 3.2 Output: one `.lean` file per `.c` file

```lean
import Std.Tactic.BVDecide     -- only when a generated theorem needs it
import Sparkle                 -- only when sparkle.h is included and a Sparkle feature is used
-- generated by c2lean from sbox.c — do not edit
set_option autoImplicit false
open Sparkle.Core.Domain Sparkle.Core.Signal   -- only with Sparkle features
variable {dom : DomainConfig}                  -- only with Sparkle features

-- c: sbox.c:3
def ...
```

Rules:

- **Ordering:** declarations are topologically sorted (Lean requires definition before use). Cycles become `mutual … end`.
- **Namespace:** none by default. `#pragma lean namespace Foo` wraps the file in `namespace Foo … end Foo` ✅.
- **`open`:** enums get an `open` so enumerators can be written unqualified ✅.
- **Comments:** C comments are kept (`//` → `--`, `/* */` → `/- -/`).
- **Toolchain:** when a project imports Sparkle, the output must compile with **that project's pinned Lean toolchain**, which may be older than 4.35 (Appendix B, item 1).

### 3.3 Function classification (D7)

| Condition on the C function body      | Lean form                          |
| ------------------------------------- | ---------------------------------- |
| No assignment to locals, no I/O       | `def f (a : A) : R := expr`        |
| Assigns to locals / arrays, no I/O    | `def f (a : A) : R := Id.run do …` |
| Contains `printf`, `puts`, `scanf`, … | `def f (a : A) : IO R := do …`     |
| Calls an `IO` function                | same, transitively                 |

Parameter assignment (`a = a + 1;` where `a` is a parameter) emits `let mut a := a` at the top of the body.

---

## 4. Types

### 4.1 Scalar types ✅

| C                                                         | Lean                                  | Synth | Notes                                                                                                                      |
| --------------------------------------------------------- | ------------------------------------- | ----- | -------------------------------------------------------------------------------------------------------------------------- |
| `int`, `signed`, `short`, `long`, `long long`             | `Int`                                 | N     | **Decided (Q1/Q2):** unbounded, no wraparound. Pure sugar. Lint W001.                                                      |
| `unsigned`, `unsigned int/short/long/long long`, `size_t` | `Nat`                                 | N     | **Decided:** `Nat`. Its subtraction truncates at 0 (lint W002).                                                            |
| `int8_t`, `int16_t`, `int32_t`, `int64_t`                 | `Int8`, `Int16`, `Int32`, `Int64`     | S     | Lean's like-named types; they wrap, natively (reference §Fixed-Precision Integers). Nothing is emulated. **Decided (Q1).** |
| `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t`             | `UInt8`, `UInt16`, `UInt32`, `UInt64` | S     | Same.                                                                                                                      |
| `__int128`, `uint128_t`                                   | ⛔                                     | —     | Lean has no 128-bit fixed integer; use `bv(128)`.                                                                          |
| `bv(N)` / `_BitVec(N)`                                    | `BitVec N`                            | S     | `N` must be a compile-time constant (**E302**). Signless — see §5.4.                                                       |
| `float`                                                   | `Float32`                             | N     | IEEE binary32.                                                                                                             |
| `double`                                                  | `Float`                               | N     | IEEE binary64.                                                                                                             |
| `long double`                                             | `Float`                               | N     | Precision lint W003.                                                                                                       |
| `_Float16`                                                | ⛔                                     | —     | No 16-bit float in Lean.                                                                                                   |
| `bool`, `_Bool`                                           | `Bool`                                | S     |                                                                                                                            |
| `char`                                                    | `Char`                                | N     | See §4.2.                                                                                                                  |
| `unsigned char`                                           | `UInt8`                               | S     |                                                                                                                            |
| `char *`, `const char *`, `char s[]`                      | `String`                              | N     | Strings are immutable in Lean (§4.3).                                                                                      |
| `void` (return type)                                      | `Unit`                                | S     | `IO Unit` when I/O is involved.                                                                                            |

**Option:** `--unsigned=int` emits `Int` for `unsigned` instead of `Nat`, for users who prefer to avoid the truncating-subtraction trap. Default stays `Nat`.

### 4.2 `char`

- `char` maps to `Char`; a char literal `'a'` maps to `'a'`.
- Arithmetic mixing `char` and integers inserts conversions: `c - '0'` → `(c.toNat - '0'.toNat)`; `char c = 65;` → `Char.ofNat 65` (lint W007).
- Bitwise operations on `char` are rejected with hint **H004**: use `uint8_t`.

### 4.3 Strings ✅

- `char *`/`char[]` and string literals map to `String`.
- Concatenation `strcat`-style → `++`; length → `s.length`.
- **Mutation of a string by index is ⛔ (E101)**; hint: use `Array Char` (or `bv`/`uint8_t` arrays for byte buffers).
- Indexing `s[i]` → `s.toList[i]!` is O(n) (lint W011).

### 4.4 Arrays ✅ (D13)

| C                                    | Lean (v0.1)                                 | Notes                                                                                                              |
| ------------------------------------ | ------------------------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| `T a[N];`                            | `Array T` initialised to `N` default values | Declared size is not part of the type (`Vector` is 🟡).                                                            |
| `T a[N] = {1,2,3,4};`                | `#[1, 2, 3, 4]`                             |                                                                                                                    |
| `T a[N] = {0};`                      | `Array.replicate N 0`                       | `Array.replicate` is used in Sparkle's source, so it exists in its toolchain.                                      |
| `T a[]`, `T *a` (+ length parameter) | `Array T`                                   | Pointer decay accepted **only** in this form; see §4.6.                                                            |
| `T a[M][N]`                          | `Array (Array T)`                           |                                                                                                                    |
| `a[i]` (read)                        | `a[i]!`                                     | Panics if out of range (lint W010). Constant in-range index on a literal array may emit `a[3]`.                    |
| `a[i] = v;`                          | `a := a.set! i v`                           | When the array is unshared, Lean's reference counting lets the update happen in place ("functional but in-place"). |
| `a[i][j] = v;`                       | `a := a.set! i (a[i]!.set! j v)`            |                                                                                                                    |

**`bool a[N]` hint (H001).** Whenever a `bool` array has a constant size, the tool emits:

```
H001  sbox.c:12  `bool a[32]` — did you mean `bv(32) a`?
      A bit-vector gets Lean's BitVec tactics (bv_decide, omega on bit ops);
      Array Bool does not.  Rewrite as `bv(32) a;`, or silence with
      #pragma lean allow_bool_array.
```

Default behaviour without an answer: translate to `Array Bool` and keep the hint. An optional interactive mode (`--ask`) prompts instead of warning.

### 4.5 Structures, enums, typedefs ✅

| C                             | Lean                                                                                      |
| ----------------------------- | ----------------------------------------------------------------------------------------- |
| `struct P { int x; int y; };` | `structure P where x : Int  y : Int deriving Repr, BEq, Inhabited`                        |
| `typedef struct {…} P;`       | same, named `P`                                                                           |
| `struct P p = {.x=1,.y=2};`   | `{ x := 1, y := 2 : P }`                                                                  |
| `struct P p = {1, 2};`        | `⟨1, 2⟩`                                                                                  |
| `p.x = 5;`                    | `p := { p with x := 5 }`                                                                  |
| `enum Color {RED, GREEN};`    | `inductive Color \| RED \| GREEN deriving Repr, DecidableEq, Inhabited` plus `open Color` |
| `enum E {A = 3, B = 7};`      | the inductive plus `def E.toNat : E → Nat`                                                |
| `typedef bv(8) byte;`         | `abbrev byte := BitVec 8`                                                                 |
| `union`                       | ⛔ (**E104**) — 🔮: reinterpretation via `BitVec`                                          |
| bit-field `int x : 3;`        | ⛔ (**E111**), hint: use `bv(3) x;`                                                        |

`deriving` clauses are added automatically so that structure and enum values can be compared and printed in equivalence checks (FPIL ch. 3 type classes).

Uninitialised locals (`int x;`) are zero-initialised in Lean (lint W004); structs use `default`.

### 4.6 Pointers

Pointers are the main source of untranslatable C. v0.1 accepts only these forms:

| Form                                                                              | Translation                                           | Status |
| --------------------------------------------------------------------------------- | ----------------------------------------------------- | ------ |
| Array parameter / decay (`T *a, size_t n`)                                        | `Array T`                                             | ✅      |
| `const T *p` read-only parameter                                                  | by-value `T`                                          | 🟡     |
| Output parameter (`void f(int *out)`)                                             | return value / tuple                                  | 🟡     |
| `char *`                                                                          | `String`                                              | ✅      |
| Anything else (pointer arithmetic, `T **`, `void *`, casts between pointer types) | ⛔ (**E101**)                                          | —      |
| Function pointers                                                                 | ⛔ (**E107**) in v0.1; 🟡 as function-typed parameters | —      |

### 4.7 Qualifiers and storage classes

`const` → ignored (global `const` becomes a `def`). `static` on functions/globals → `private`. `inline` → `@[inline]`. `restrict` → ignored. `volatile`, `register`, `_Atomic`, `_Thread_local` → ⛔ (**E109/E113**).

---

## 5. Expressions

Operators keep their Lean meaning (D4): the table lists the Lean spelling, not a C-semantics emulation.

### 5.1 Operators ✅

| C                                     | Lean on `Int`/`Nat`                  | Lean on `BitVec N` / `UIntN` / `IntN` | Notes                                                                       |
| ------------------------------------- | ------------------------------------ | ------------------------------------- | --------------------------------------------------------------------------- |
| `+ - *`                               | same                                 | same (wrapping)                       |                                                                             |
| `/` , `%`                             | `/` , `%`                            | `/` , `%` (unsigned on `BitVec`)      | Lean's own semantics; division by zero is `0` (lint W005).                  |
| `== != < <= > >=`                     | `== != < <= > >=` (via `decide`)     | same                                  | On `BitVec` the ordering is unsigned; see §5.4 for signed.                  |
| `&& \|\| !`                           | same                                 | same                                  |                                                                             |
| `& \| ^ ~`                            | `&&&` `\|\|\|` `^^^` — on `Nat` only | `&&&` `\|\|\|` `^^^` `~~~`            | Bitwise on `Int` is ⛔ (**E303**); use `uintN_t` or `bv`.                    |
| `<< >>`                               | `<<<` `>>>` on `Nat`                 | `<<<` `>>>`                           | `>>` is logical on `BitVec`/`UIntN`; arithmetic shift via `bv_sshr` (§5.4). |
| `?:`                                  | `if c then a else b`                 | same                                  |                                                                             |
| `x++`, `x--`, `x += e` …              | statement `x := x + 1`               | same                                  | Allowed **only** as statements or in `for` headers (**E110** otherwise).    |
| `a = b` as expression, comma operator | ⛔ (**E103**)                         |                                       |                                                                             |
| `sizeof`                              | ⛔ (**E112**)                         |                                       | 🟡 for `bv`/array sizes.                                                    |

### 5.2 Truthiness

C allows any scalar in a condition. The tool inserts the explicit test:

| C condition on             | Lean                                   |
| -------------------------- | -------------------------------------- |
| `bool`                     | as is                                  |
| `Int`/`Nat`/`UIntN`/`IntN` | `x != 0`                               |
| `BitVec N`                 | `x != 0#N`                             |
| `Float`                    | `x != 0.0`                             |
| pointer / `String`         | ⛔ for pointers; `s != ""` for `String` |

### 5.3 Conversions and casts ✅

Lean does not convert implicitly, so a cast written in C, or a mix of types in one expression, becomes an explicit Lean conversion chosen from the *types as written*. C's *usual arithmetic conversions* are not replicated.

| From → to                     | Lean                                           | Lint                         |
| ----------------------------- | ---------------------------------------------- | ---------------------------- |
| `Nat` → `Int`                 | `(↑x : Int)`                                   | —                            |
| `Int` → `Nat` (`(unsigned)x`) | `x.toNat`                                      | W009 (clamps negatives to 0) |
| `Nat`/`Int` → `UIntN`/`IntN`  | `UInt8.ofNat x`, `Int8.ofInt x`                | —                            |
| `UIntN` → `Nat`               | `x.toNat`                                      | —                            |
| `IntN` → `Int`                | `x.toInt`                                      | —                            |
| `Nat` → `BitVec N`            | `BitVec.ofNat N x`                             | —                            |
| `BitVec N` → `Nat`            | `x.toNat`                                      | —                            |
| `Int`/`Nat` → `Float`         | `Float.ofInt x`, `Float.ofNat x`               | —                            |
| `Float` → `Int32` etc.        | `Float.toInt32 f` (truncates, NaN → 0, clamps) | —                            |
| `Float` ↔ `Float32`           | `toFloat32`, `toFloat`                         | —                            |

Arithmetic on `uint8_t` is done in `UInt8` (exact width), where C would promote to `int` (lint W008). This matches HDL width semantics.

### 5.4 Bit-vector helpers ✅

`bv(N)` is **signless**, like Verilog's default. Operations whose meaning depends on signedness have explicit macros. All are parse-only declarations in `lean.h`.

| C helper                         | Lean                                | Notes                                                          |
| -------------------------------- | ----------------------------------- | -------------------------------------------------------------- |
| `BV(8, 0x63)`                    | `0x63#8`                            | literal with explicit width (Sparkle uses the same `x#N` form) |
| `bv_cat(a, b)`                   | `a ++ b`                            | widths add                                                     |
| `bv_ext(x, hi, lo)`              | `x.extractLsb' lo (hi - lo + 1)`    | the form Sparkle uses for slicing                              |
| `bv_bit(x, i)`                   | `x.getLsbD i`                       | `Bool`; name to verify                                         |
| `bv_zext(N, x)`                  | `x.setWidth N`                      | 4.35 name to verify (older: `zeroExtend`)                      |
| `bv_sext(N, x)`                  | `x.signExtend N`                    | to verify                                                      |
| `bv_rotl(x, k)`, `bv_rotr(x, k)` | `x.rotateLeft k`, `x.rotateRight k` | to verify                                                      |
| `bv_slt(a, b)`, `bv_sle(a, b)`   | `a.slt b`, `a.sle b`                | signed compare (used by Sparkle)                               |
| `bv_ult(a, b)`, `bv_ule(a, b)`   | `a.ult b`, `a.ule b`                | explicit unsigned compare                                      |
| `bv_sdiv(a, b)`                  | `a.sdiv b`                          | to verify                                                      |
| `bv_sshr(x, k)`                  | `x.sshiftRight k.toNat`             | arithmetic shift (Sparkle uses `sshiftRight … toNat`)          |

Mixing widths in one operation is an error (**E301**) with the hint to use `bv_zext`/`bv_sext`/`bv_ext`.

---

## 6. Statements and control flow ✅

All function bodies that use these constructs are emitted in `do` notation (reference ch. *Functors, Monads and `do`-Notation*).

| C                      | Lean                                           | Synth        |
| ---------------------- | ---------------------------------------------- | ------------ |
| `T x = e;`             | `let x : T' := e` (or `let mut` if reassigned) | S            |
| `x = e;`               | `x := e`                                       | S            |
| `if (c) {…} else {…}`  | `if c' then … else …`                          | S            |
| `switch (e) {…}`       | `match e with …`                               | S            |
| `for (…)`              | see §6.1                                       | S if bounded |
| `while (c) {…}`        | `while c' do …`                                | N            |
| `do {…} while (c);`    | `repeat … ; if !c' then break`                 | N            |
| `break;` / `continue;` | `break` / `continue`                           | S            |
| `return e;`            | `return e`                                     | S            |
| `goto`, labels         | ⛔ (**E102**), except the theorem label of §9.3 | —            |

### 6.1 `for` loops

Two translations, chosen automatically:

1. **Range-for** (preferred, proof-friendly). Pattern: `for (T i = A; i < B; i++)` (or `<=`, or `i += K`) where `A`, `B`, `K` are constants or unsigned/`Nat`-valued expressions (converted with `.toNat`), and the body does not assign `i`:
   
   ```lean
   for i in [A:B] do          -- `<=` becomes [A:B+1];  `+= K` becomes [A:B:K]
     …
   ```
   
   If the C loop variable is `int`, the body sees `(i : Int)` via a coercion. Range-notation spelling to be confirmed (Appendix B); Sparkle's source uses `for i in [sz:t + 1] do`, so this form exists in its toolchain.

2. **General for** (fallback): any other header becomes a `while` loop with the initialiser before and the step at the end of the body (and before every `continue`).

**Synth:** only range-for with constant bounds is allowed; anything emitted through `while` is **E202**.

### 6.2 `switch`

```c
switch (op) { case 0: case 1: r = a; break;
              case 2:         r = b; break;
              default:        r = 0; }
```

```lean
match op with
| 0 | 1 => r := a
| 2     => r := b
| _     => r := 0
```

Grouped empty `case` labels become or-patterns (FPIL ch. 4, *Additional Conveniences*). Non-empty fall-through is ⛔ (**E105**). A missing `default` emits `| _ => pure ()`.

---

## 7. Functions and program structure

### 7.1 Definitions ✅

```c
int add(int a, int b) { return a + b; }
```

```lean
-- c: x.c:1
def add (a b : Int) : Int := a + b
```

- Adjacent parameters of the same type are merged `(a b : Int)` (FPIL ch. 4, *Additional Conveniences*, shared argument types).
- Forward declarations (prototypes) are dropped; ordering is solved by the topological sort (§3.2).
- Mutual recursion → `mutual … end` ✅.
- Recursion is allowed in `model`, rejected in `synth` (**E201**). If Lean cannot prove termination, the user adds `[[lean::partial]]` → `partial def` ✅.
- `int main(void)` → `def main : IO UInt32`; `void main`/no return → `IO Unit` ✅. `main(int argc, char **argv)` → `def main (args : List String)` 🟡.

### 7.2 `extern` declarations ✅ (key for your use case)

`extern T f(T1);` means **"Lean already defines `f`."** Nothing is emitted for the definition. There are two flavours.

#### 7.2.1 Value-level extern (default)

The Lean function is an ordinary function on values. This is the right flavour for pure reference models written in Lean (including Sparkle's "pure spec" functions) and for any plain Lean function.

```c
extern bv(8) sbox_spec(bv(8));
```

```lean
example : BitVec 8 → BitVec 8 := sbox_spec     -- emitted with --check-externs
```

#### 7.2.2 `HW` extern: a Sparkle module

*Requires `#include <sparkle.h>` (§17); `HW` and `HW_LAT` are not available from `lean.h`.*

A Sparkle module is a function between **streams**. In Sparkle, `Signal dom α` is a structure wrapping a function `Nat → α` — "time step ↦ value" — and `Signal.map` is the combinational (pointwise) lift. C has no notion of time, so the declaration states how to read the module:

```c
HW      extern bv(8) sboxHW(bv(8));        // combinational: output depends on the same cycle's input
HW_LAT(1) extern bv(8) mulHW(bv(8), bv(8)); // pipelined: output appears 1 cycle after input
```

**Meaning of the declaration.** Each `bv(N)`/`bool` in the signature is wrapped in `Signal dom`:

```lean
-- sboxHW : {dom : DomainConfig} → Signal dom (BitVec 8) → Signal dom (BitVec 8)
example {dom : DomainConfig} :
    Signal dom (BitVec 8) → Signal dom (BitVec 8) := sboxHW   -- emitted with --check-externs
```

**Value-level call rule (the lift).** In C, `sboxHW(x)` with a *value* `x` means: drive the module with the constant stream `x`, and read its output at cycle `L` (the declared latency; `0` for `HW`):

```lean
(sboxHW (Signal.lit dom x)).atTime 0              -- HW
(mulHW (Signal.lit dom a) (Signal.lit dom b)).atTime 1   -- HW_LAT(1)
```

`Signal.lit dom x` is Sparkle's constant signal with an explicit domain (Sparkle's own troubleshooting notes that plain `Signal.pure` can leave the domain unresolved), and `atTime t` samples a signal at cycle `t`. For a combinational module this equals `sboxHW`'s function on values; for a pipeline it is the steady-state response to a held input.

Restrictions:

- A value-level call of a module with internal state is only meaningful if its latency is declared. Calling a `HW` extern that the tool cannot classify is **E304**.
- Struct and array arguments at the hardware boundary (tuple bundles, `HWVector`) are 🟡 (**Q3**).

Rationale for this choice versus the alternatives is in §9.1.

### 7.3 Globals

| C                              | Lean                          | Status                  |
| ------------------------------ | ----------------------------- | ----------------------- |
| `const int N = 8;`             | `def N : Int := 8`            | ✅                       |
| `const uint8_t T[4] = {…};`    | `def T : Array UInt8 := #[…]` | ✅                       |
| `#define N 8`                  | expanded by the preprocessor  | ✅                       |
| mutable global, `static` local | ⛔ (**E106**)                  | 🟡 as `StateM`/`IO.Ref` |

### 7.4 Constants as widths 🟡

`#define W 8` used in `bv(W)` becomes `abbrev W : Nat := 8` so widths stay symbolic and the Lean output reads `BitVec W`. (Sparkle's roadmap also lists "retained dimensions", i.e. symbolic widths through its compiler.)

### 7.5 Naming

Identifiers are preserved. Lean keywords used as C identifiers fail loudly.

---

## 8. I/O and `printf` ✅

### 8.1 Inside a function (function becomes `IO`)

| C                                  | Lean                         |
| ---------------------------------- | ---------------------------- |
| `printf("x");`                     | `IO.print "x"`               |
| `printf("x\n");`                   | `IO.println "x"`             |
| `printf("a=%d\n", a);`             | `IO.println s!"a={a}"`       |
| `puts(s);`                         | `IO.println s`               |
| `putchar(c);`                      | `IO.print c`                 |
| `fprintf(stderr, …);`              | `IO.eprint` / `IO.eprintln`  |
| `scanf`, `getchar`, `fgets(stdin)` | 🟡 `(← IO.getStdin).getLine` |
| `fopen`/`fclose`/file I/O          | 🟡 `IO.FS`                   |
| `exit(n)`                          | 🟡 `IO.Process.exit`         |

`IO.print`/`IO.println` take any `ToString` value and print to standard output; `IO.eprint`/`IO.eprintln` write to standard error (reference ch. *IO* → *Console Output*).

**Format specifiers** are translated into `s!` interpolation:

| Spec                         | Interpolation       | Notes                                                               |
| ---------------------------- | ------------------- | ------------------------------------------------------------------- |
| `%d %i %u %ld %lu`           | `{x}`               |                                                                     |
| `%s`                         | `{s}`               |                                                                     |
| `%c`                         | `{c}`               |                                                                     |
| `%f %g %e`                   | `{x}`               | Lean's float rendering differs; precision/width ignored (lint W006) |
| `%x`, `%X`                   | `{hexOf x}`         | helper from the generated prelude; `BitVec` → `x.toHex`             |
| `%%`                         | `%`                 |                                                                     |
| width/flags (`%08x`, `%-5d`) | ignored (lint W006) | 🟡 padding helpers                                                  |

### 8.2 At file scope ("script mode")

Following your examples, a bare `printf` at file scope has no enclosing function and becomes an elaboration-time check:

| C (file scope)          | Lean                           |
| ----------------------- | ------------------------------ |
| `printf("%d", 2+3);`    | `#eval 2+3`                    |
| `printf("x");`          | `#eval IO.print "x"`           |
| `printf("v=%d", f(3));` | `#eval IO.println s!"v={f 3}"` |

Rule: a format string that is **exactly one specifier** becomes `#eval <expr>` (Lean displays the value with its own formatting, so strings appear quoted); anything else becomes `#eval IO.println s!"…"`.

Note the semantic difference: `#eval` output appears when Lean checks the file (editor, `lake build`), not at program run time.

---

## 9. Verification constructs

Generated theorems are the only proof content (D11).

### 9.1 How C relates to Sparkle (design rationale for Q3)

Facts about Sparkle (from its source and docs):

- `Signal dom α` wraps `val : Nat → α` (cycle ↦ value); `atTime s t = s.val t`.
- `Signal.map f s` applies `f` pointwise per cycle (combinational logic); Sparkle overloads `+ - * &&& ||| ^^^ ++ ~~~ <<< >>>` on `Signal dom (BitVec n)` through this lift.
- `Signal.register init s` is a one-cycle delay; feedback goes through `Signal.loop`.
- Sparkle's methodology is *pure spec first*: define behaviour as pure functions, prove properties, implement in the Signal DSL, then (optionally) prove refinement "impl ≈ spec".
- Sparkle ships equivalence commands: `#verify_eq f g` for pure `BitVec` functions, and `#verify_eq_at (cycles := N) (latency := L) impl spec` for Signal-DSL circuits, which expands to a conjunction of `(impl inputs).val (t + L) = (spec inputs).val t` for `t ∈ [0, N)`, each discharged with `simp` plus `bv_decide`.

**Options considered**

| Option                                                | Idea                                                                                                                                                                                 | Verdict                                                                                                                                                                                                            |
| ----------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **A. Value-level C, lift at the boundary** *(chosen)* | C functions are pure on `BitVec`/`Bool`. A `HW` extern declares how to read a module (latency). Equivalence theorems are stated with `atTime` on constant or symbolic input streams. | Keeps C time-free; matches Sparkle's "pure spec" step; theorems stay plain Lean (no dependency on Sparkle's custom commands).                                                                                      |
| B. Emit Sparkle's commands only                       | Generate `#verify_eq` / `#verify_eq_at` and nothing else.                                                                                                                            | Great reuse and cycle-accurate checks, but ties the output to command syntax I could not fully verify, and the commands are not available without Sparkle. Offered as an **optional lowering** (`--sparkle-cmds`). |
| C. Stream-level equality                              | State `∀ s, sboxHW s = sbox_impl s`.                                                                                                                                                 | Equality of `Signal` structures needs function extensionality over streams, which is awkward for SAT tactics. Rejected.                                                                                            |
| D. C describes Signal code                            | Make the C front-end produce Signal-DSL hardware.                                                                                                                                    | Out of scope (§1.3): Sparkle's synthesis elaborator accepts only specific patterns.                                                                                                                                |

**Decision:** Option A as the normative semantics; Option B as an optional lowering for users who have Sparkle installed.

### 9.2 Theorem shapes ✅

Let `f`, `g` be the two sides of the check, `x` the symbolic input of width `N`, and `L` a module's latency (`0` for value-level functions and `HW`).

| Left               | Right        | Generated statement                                                                   |
| ------------------ | ------------ | ------------------------------------------------------------------------------------- |
| value              | value        | `∀ x : BitVec N, f x = g x`                                                           |
| `HW` / `HW_LAT(L)` | value        | `∀ x : BitVec N, (f (Signal.lit dom x)).atTime L = g x`                               |
| `HW_LAT(L₁)`       | `HW_LAT(L₂)` | `∀ x : BitVec N, (f (Signal.lit dom x)).atTime L₁ = (g (Signal.lit dom x)).atTime L₂` |

This is exactly the value-level call rule of §7.2.2, so the C expression `sboxHW(a) == sbox_spec(a)` and the generated theorem agree by construction.

Default proof script:

```lean
theorem <name> {dom : DomainConfig} : ∀ x : BitVec N, <statement> := by
  intro x
  simp only [<unfolding set for the Signal definitions>]
  bv_decide
```

Notes that affect real use:

- `bv_decide` closes goals about `BitVec`/`Bool` by SAT and checks the certificate inside Lean; on failure it reports a **counterexample**. It uses `Lean.ofReduceBool`, so the Lean compiler is part of the trusted base.
- An **unknown definition is treated as an unconstrained variable**, so the `simp only` step must unfold the module definitions (and Sparkle's `Signal` lemmas) first; otherwise `bv_decide` reports a spurious counterexample. The unfolding set is derived from the names mentioned in the statement; the exact lemma names come from Sparkle's `Equivalence.lean` (Appendix B, item 4).
- Sparkle's known-issues file reports that `bv_decide` can hang under `lake build` but works interactively (`lake env lean`). Treat generated theorem files as interactive-check targets until re-tested on your toolchain (Appendix B, item 3). `bv_decide?` can cache a proof to avoid re-running the solver.
- Goals over `UIntN`/`IntN` can be reduced to `BitVec` by an option of `bv_decide`'s preprocessing; the exact configuration is to verify.

### 9.3 Explicit form ✅

```c
VERIFY_FORALL(bv(8) a) { sboxHW(a) == sbox_spec(a); }
```

→ the second row of §9.2.

- The body must be a single boolean expression (or an `&&` chain; each conjunct is proved).
- The binder list may contain several `bv(N)`/`uintN_t`/`bool` variables.
- Total bit-width of binders above 64 produces lint **W012**.

### 9.4 Loop-pattern form 🟡 (good-to-have)

```c
sbox_equiv:
for (int i = 0; i < 256; i++) { sboxHW(i) == sbox_impl(i); }
```

```lean
theorem sbox_equiv {dom : DomainConfig} : ∀ i : BitVec 8,
    (sboxHW (Signal.lit dom i)).atTime 0 = (sbox_impl (Signal.lit dom i)).atTime 0 := by
  intro i
  simp only [sboxHW, sbox_impl, …]
  bv_decide
```

Recognition rule (all must hold):

1. file scope, or inside a function marked `[[lean::verify]]`;
2. `for` with constant range `[0, B)`;
3. body is a single boolean expression using the loop variable;
4. the loop variable is passed unmodified to callee parameters of a `BitVec N` (or `uintN_t`) type.

Then:

- If `B == 2^N` (here 256 = 2^8), the loop **is** the full domain → `∀ i : BitVec N`. If not, `∀ i : BitVec N, i.toNat < B → …` and hint **H002** suggests `VERIFY_FORALL`.
- The loop variable's C type is `int` in the source; it takes the type of the callee's parameter. A mismatch among callees is **E301**.
- **Theorem name:** a C label immediately before the loop (`sbox_equiv:`) names the theorem (labels are otherwise unused because `goto` is ⛔). Without a label the name is `<common-prefix>_equiv` of the two callee names, or `<a>_eq_<b>` if they share no prefix.
- A loop that does not match is **not** silently ignored: at file scope it is an **E103** error "unrecognised verification loop".

### 9.5 Bounded sequential checks 🟡

For pipelined or stateful modules where a steady-state constant-input comparison is not enough, the user states a cycle bound and latency, mirroring Sparkle's `#verify_eq_at`:

```c
VERIFY_AT(8, 1)  { mulHW(a, b) == mul_spec(a, b); }   // 8 cycles, latency 1
```

Normative statement: for every symbolic per-cycle input streams, `(impl inputs).atTime (t + L) = (spec inputs).atTime t` for each `t < cycles`. With `--sparkle-cmds` the tool may emit `#verify_eq_at (cycles := 8) (latency := 1) impl spec` instead (syntax to verify, Appendix B item 4). Unbounded (induction-based) sequential proofs are 🔮.

### 9.6 Tactic control 🟡

| Mechanism                                                                                 | Effect                                                            |
| ----------------------------------------------------------------------------------------- | ----------------------------------------------------------------- |
| `[[lean::proof("by intro x; simp [sboxHW]; bv_decide")]]` before a verification statement | replaces the default tactic script                                |
| `--prove=none`                                                                            | emit `theorem … := by sorry` with warning **W013** (never silent) |
| default fallback chain                                                                    | `first                                                            |

### 9.7 Out of scope

- Properties other than combinational/bounded-cycle equality (safety/liveness proofs of the kind in Sparkle's `Verification_Framework`).
- Anything needing induction or user lemmas.

---

## 10. Coverage map — *Functional Programming in Lean* (priority 1)

| FPIL chapter                                  | Lean feature                                                                          | C-side spelling                                                    | Status                         |
| --------------------------------------------- | ------------------------------------------------------------------------------------- | ------------------------------------------------------------------ | ------------------------------ |
| **1. Getting to Know Lean**                   | `#eval` expressions                                                                   | file-scope `printf`; `LEAN_STMT("#eval …")`                        | ✅                              |
|                                               | Types: `Nat`, `Int`, `Float`, `String`, `Bool`, `Char`, `Unit`                        | §4.1                                                               | ✅                              |
|                                               | Products (tuples)                                                                     | struct; multi-return via out-params                                | ✅ / 🟡                         |
|                                               | Sum types, `Option`                                                                   | tagged unions; NULL-return idiom                                   | 🔮 / 🟡                        |
|                                               | `def`, arguments, `let`                                                               | functions, locals                                                  | ✅                              |
|                                               | Lambdas (`fun`), higher-order functions                                               | function pointers                                                  | 🟡                             |
|                                               | `namespace`, `where` helpers                                                          | `#pragma lean namespace`; top-level helpers                        | ✅                              |
|                                               | `structure`, update syntax `{ s with … }`, projections                                | `struct`, `s.f = v`, `s.f`                                         | ✅                              |
|                                               | `inductive` (enumerations), `match`, recursion                                        | `enum`, `switch`, recursive functions                              | ✅                              |
|                                               | `inductive` with payloads, structural recursion on them                               | tagged unions                                                      | 🔮                             |
|                                               | Polymorphism (`List α`, implicit arguments)                                           | generics                                                           | 🟡 (§11.2)                     |
|                                               | Anonymous constructors `⟨ , ⟩`, `if let`, string interpolation                        | struct initialisers; `printf`                                      | ✅                              |
| **2. Hello, World!**                          | `main : IO Unit`, `IO.println`                                                        | `main`, `printf`                                                   | ✅                              |
|                                               | stdout/stderr                                                                         | `fprintf(stderr, …)`                                               | ✅                              |
|                                               | stdin, `IO.FS.Stream.getLine`                                                         | `scanf`, `fgets`                                                   | 🟡                             |
|                                               | `let mut`, `while`, `for`, early `return` in `do`                                     | §6                                                                 | ✅                              |
|                                               | arrays, `Array.set!`, in-place update                                                 | §4.4                                                               | ✅                              |
|                                               | string interpolation `s!"…"`                                                          | `printf` specifiers                                                | ✅                              |
|                                               | command-line arguments                                                                | `argc/argv`                                                        | 🟡                             |
| **Interlude: Propositions, Proofs, Indexing** | `xs[i]`, `xs[i]!`, `xs[i]?`, `xs[i]'h`                                                | `a[i]` → `a[i]!`                                                   | ✅ (`--index=proof` 🟡)         |
|                                               | `Prop` vs `Bool`, decidability                                                        | C conditions → `Bool`; theorem bodies → `Prop`                     | ✅                              |
| **3. Overloading and Type Classes**           | `ToString`, `Repr`, `BEq`, `Hashable`                                                 | auto-`deriving` on structs/enums                                   | ✅                              |
|                                               | `instance`, user type classes                                                         | `LEAN_DECL`                                                        | ✅ (passthrough)                |
|                                               | coercions                                                                             | explicit casts (§5.3)                                              | ✅                              |
|                                               | `GetElem` / array indexing                                                            | §4.4                                                               | ✅                              |
|                                               | overloaded operators                                                                  | C++                                                                | 🔮                             |
| **4. Monads**                                 | `do` notation                                                                         | all function bodies                                                | ✅                              |
|                                               | `Option` / `Except` as error channel                                                  | error-code returns (`-1`, `NULL`)                                  | 🟡                             |
|                                               | `StateM`, `IO.Ref`                                                                    | globals, `static` locals                                           | 🟡                             |
|                                               | Additional conveniences: shared argument types, or-patterns, leading-dot constructors | merged parameters; grouped `case`s; qualified/`open`ed enumerators | ✅ (leading-dot is not emitted) |
| **5. Functors, Applicative Functors, Monads** | (`Signal` is a Functor/Applicative/Monad in Sparkle)                                  | handled only through `HW` externs (§7.2)                           | 🔮 for user-defined            |
| **6. Monad Transformers**                     |                                                                                       | none in C                                                          | 🔮                             |
| **7. Programming with Dependent Types**       | `BitVec n`, `Fin n`                                                                   | `bv(N)`, bounded indices                                           | ✅ (`bv`)                       |
|                                               | `Vector α n`                                                                          | fixed-size arrays                                                  | 🟡                             |
|                                               | indexed families, custom dependent types                                              | `LEAN_DECL`                                                        | 🔮                             |
| **Interlude: Tactics, Induction, Proofs**     | tactics                                                                               | `[[lean::proof(…)]]` passthrough; generated scripts of §9          | 🟡 / ✅                         |
| **8. Programming, Proving, and Performance**  | tail recursion, accumulators                                                          | loops in `do`                                                      | ✅                              |
|                                               | `partial`, `termination_by`                                                           | `[[lean::partial]]`                                                | ✅ / 🟡                         |
|                                               | `@[inline]`, `@[specialize]`                                                          | `inline`, `[[lean::inline]]`                                       | ✅                              |
|                                               | functional-but-in-place updates                                                       | `a = a.set! …` pattern                                             | ✅                              |
| **9. Next Steps**                             | —                                                                                     | —                                                                  | n/a                            |

---

## 11. Coverage map — Lean syntax (priority 2)

Organised by the Language Reference. **Core features first; convenience features (polymorphism, overloading) second.**

### 11.1 Tier A — core (v0.1)

| Reference area           | Lean construct                                                                                               | C spelling                                                               | Status             |
| ------------------------ | ------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------ | ------------------ |
| Definitions              | `def`, `abbrev`                                                                                              | function; `typedef`                                                      | ✅                  |
|                          | `theorem`, `example`                                                                                         | §9                                                                       | ✅ (generated only) |
|                          | `partial def`, `mutual`                                                                                      | `[[lean::partial]]`; mutual recursion                                    | ✅                  |
|                          | `private`, `protected`                                                                                       | `static`                                                                 | ✅ (`private`)      |
| Terms                    | `let`, `let mut`, `fun`, `if…then…else`                                                                      | locals; `?:`, `if`                                                       | ✅ (`fun`: 🟡)      |
|                          | `match`, or-patterns                                                                                         | `switch`                                                                 | ✅                  |
|                          | anonymous constructor `⟨ , ⟩`, struct instance `{ x := … }`, `with` update                                   | initialisers, `s.f = v`                                                  | ✅                  |
|                          | numeric literals (decimal, hex, binary), `x#N` bit-vector literals                                           | `42`, `0xFF`, `0b1010`, `BV(N,v)`                                        | ✅                  |
|                          | string/char literals, `s!` interpolation                                                                     | literals, `printf`                                                       | ✅                  |
| Basic Types              | `Nat`, `Int`, `Int8–64`, `UInt8–64`, `BitVec`, `Float`, `Float32`, `Char`, `String`, `Bool`, `Unit`, `Array` | §4                                                                       | ✅                  |
|                          | `Option`, tuples, sums, `List`, `Fin`, subtypes                                                              |                                                                          | 🟡 / 🔮            |
| `do`-notation            | `let mut`, `for … in`, `while`, `repeat`, `break`, `continue`, `return`                                      | §6                                                                       | ✅                  |
| IO                       | `IO.print(ln)`, `IO.eprint(ln)`, `main`                                                                      | §8                                                                       | ✅                  |
| Namespaces and Sections  | `namespace`, `open`, `variable`                                                                              | `#pragma lean namespace`, enums; emitted `variable {dom}` for HW externs | ✅                  |
| Source Files and Modules | `import`                                                                                                     | emitted automatically (`Std.Tactic.BVDecide`, `Sparkle`)                 | ✅                  |
| Attributes               | `@[inline]`, `@[simp]`, …                                                                                    | `[[lean::inline]]`, `LEAN_DECL`                                          | ✅ / passthrough    |
| Tactic Proofs            | `bv_decide`, `decide`, `intro`, `simp only`                                                                  | §9                                                                       | ✅                  |
| Escape hatch             | any Lean                                                                                                     | `LEAN_DECL`, `LEAN_STMT`, `LEAN_EXPR`                                    | ✅                  |

### 11.2 Tier B — convenience (v0.2) 🟡

- **Polymorphism.** C has none, so a template-like macro is proposed:
  
  ```c
  GENERIC(T) T max2(T a, T b) { return a > b ? a : b; }
  ```
  
  → `def max2 {T : Type} [Ord T] (a b : T) : T := if a > b then a else b`. Typeclass constraints are inferred from the operators used (`+` → `[Add T]`, `<` → `[Ord T]`/`[LT T]`, `==` → `[BEq T]`). Open: how to spell several type parameters and explicit constraints (**Q5**).
- **Overloading.** C11 `_Generic` selection maps to `match`-style dispatch or separate definitions in a namespace; true overloading arrives with C++ (§15).
- **`Option`/`Except`** conventions for error-code returns.
- **Lambdas / function pointers** as function-typed parameters.
- **Tuples / multiple return values** via out-parameters.
- **`List`** for linked structures; **`Vector α n`** for fixed arrays.
- **`where`-local helpers** (C has no nested functions; GNU extension only if wanted).

### 11.3 Tier C — advanced (v0.3+) 🔮

Tagged unions → `inductive` with payloads; user type classes and instances; dependent types and indexed families; monad stacks and transformers; `mvcgen`/`grind`-based verification of imperative-style code; universe polymorphism (never exposed; passthrough only). `axiom` is **never** generated (D5).

---

## 12. C standard library mapping (priority 3)

Each row: C function → Lean. Names are to be checked against 4.35 where flagged in Appendix B.

### 12.1 `<math.h>` — `Float`/`Float32` (all confirmed present in the reference)

`sqrt` → `Float.sqrt`; `cbrt` → `Float.cbrt`; `pow` → `Float.pow`; `exp`, `exp2` → `Float.exp`, `Float.exp2`; `log`, `log2`, `log10`; `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`, `tanh` (+ inverse hyperbolics); `floor`, `ceil`, `round`; `fabs` → `Float.abs`; `isnan`/`isinf`/`isfinite` → `Float.isNaN`/`isInf`/`isFinite`. The `…f` variants map to `Float32.*`. `fmod`, `hypot`, `lround` → ⛔/🟡 (not confirmed).

Transcendental functions are opaque to Lean's logic and do not reduce in the kernel; they are fine for `#eval`, useless in proofs.

### 12.2 `<stdlib.h>`, `<stdint.h>`, `<limits.h>`

| C                                      | Lean                                           | Status |
| -------------------------------------- | ---------------------------------------------- | ------ |
| `abs`, `labs`                          | `Int.natAbs` (+ cast)                          | ✅      |
| `min`, `max` (macro idiom)             | `min`, `max`                                   | ✅      |
| `atoi`                                 | `String.toInt!`                                | 🟡     |
| `INT32_MAX`, `INT32_MIN` (and 8/16/64) | `Int32.maxValue.toInt`, `Int32.minValue.toInt` | ✅      |
| `UINT8_MAX` …                          | `UInt8.size - 1`                               | ✅      |
| `malloc`, `calloc`, `realloc`, `free`  | ⛔ (**E108**)                                   | —      |
| `rand`, `srand`                        | ⛔ in v0.1; 🔮 `IO.rand`                        | —      |
| `exit`                                 | 🟡                                             | —      |

### 12.3 `<string.h>`

| C                                       | Lean                         | Status                               |
| --------------------------------------- | ---------------------------- | ------------------------------------ |
| `strlen`                                | `String.length` (characters) | ⚠️ byte-length differs for non-ASCII |
| `strcmp`                                | `compare`                    | ✅                                    |
| `strcat`                                | `++`                         | ✅                                    |
| `strcpy`, `strncpy`, `memcpy`, `memset` | ⛔ / 🟡 as array ops          | —                                    |

### 12.4 `<ctype.h>`

`isdigit` → `Char.isDigit`; `isalpha` → `Char.isAlpha`; `isalnum` → `Char.isAlphanum`; `isspace` → `Char.isWhitespace`; `toupper`/`tolower` → `Char.toUpper`/`Char.toLower`.

### 12.5 `<assert.h>`

`assert(c)` → `assert! c` inside `IO`/`do` code ✅ (a failed `assert!` panics at run time and is **not** a proof obligation).

### 12.6 Read In (`<stdio.h>`)

`scanf`, read/write files. 

### 12.7 Bit tricks (`__builtin_*`) 🟡

`__builtin_popcount`, `__builtin_clz`, `__builtin_parity` → small generated helper definitions over `BitVec` (no direct Lean primitive is assumed).

---

## 13. Diagnostics

### 13.1 Message format

```
<severity><code>  file.c:LINE:COL  <what>
        why:  <one sentence>
        fix:  <concrete rewrite>
```

Errors (E) and hints (H) are always shown. **Lints (W001–W013) are off by default** and enabled with `--lint=c-semantics` (W001–W011) or by the feature that triggers them (W012, W013). Each lint is reported once per kind per file.

### 13.2 Catalogue

| Code | Severity      | Trigger                                                                                              | Fix hint                                                                   |
| ---- | ------------- | ---------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------- |
| E101 | error         | pointer use outside §4.6                                                                             | use array parameter or return value                                        |
| E102 | error         | `goto` / unused label                                                                                | restructure with loops                                                     |
| E103 | error         | assignment as expression, comma operator, unrecognised verification loop                             | split into statements                                                      |
| E104 | error         | `union`                                                                                              | use `bv(N)` and `bv_ext`/`bv_cat`                                          |
| E105 | error         | `switch` fall-through                                                                                | add `break` or merge cases                                                 |
| E106 | error         | mutable global or `static` local                                                                     | pass state as parameter                                                    |
| E107 | error         | function pointer (v0.1)                                                                              | pass a named function (🟡)                                                 |
| E108 | error         | `malloc`/`free`                                                                                      | fixed-size array                                                           |
| E109 | error         | `volatile`, inline asm, variadics (except `printf` family), `setjmp`                                 | remove                                                                     |
| E110 | error         | `++`/`--` inside an expression                                                                       | separate statement                                                         |
| E111 | error         | bit-field                                                                                            | `bv(N)` member                                                             |
| E112 | error         | `sizeof`                                                                                             | pass the size explicitly                                                   |
| E113 | error         | threads/atomics                                                                                      | —                                                                          |
| E201 | error (synth) | recursion                                                                                            | unroll or bound                                                            |
| E202 | error (synth) | loop not reducible to range-for                                                                      | constant-bound `for`                                                       |
| E203 | error (synth) | float types                                                                                          | fixed-point with `bv(N)`                                                   |
| E204 | error (synth) | `String`/`char *`                                                                                    | `bv(8)` arrays                                                             |
| E205 | error (synth) | I/O (`printf`, …)                                                                                    | mark function `NOSYNTH`                                                    |
| E208 | error (synth) | unbounded `int`/`unsigned`                                                                           | `intN_t`/`uintN_t`/`bv(N)`                                                 |
| E301 | error         | bit-width mismatch                                                                                   | `bv_zext`, `bv_sext`, `bv_ext`                                             |
| E302 | error         | non-constant `bv` width                                                                              | use a constant or `#define`                                                |
| E303 | error         | bitwise operator on `Int`                                                                            | `uintN_t` or `bv(N)`                                                       |
| E304 | error         | value-level call of a `HW` extern whose latency is unknown, or of a stateful module with no `HW_LAT` | declare `HW` or `HW_LAT(n)`; stateful modules are checked with `VERIFY_AT` |
| E401 | error         | Sparkle name (`HW`, `REG`, `NEXT`, …) used without `#include <sparkle.h>`                            | add the include (§17.2)                                                    |
| E402 | error         | `REG`/`NEXT` outside an `HW` function body                                                           | move into an `HW` body                                                     |
| E403 | error         | `NEXT` target is not a `REG`, or the same register has two `NEXT`s                                   | one `NEXT` per register; use `?:` for conditions                           |
| E404 | error         | `HW` body calls a non-`HW` function                                                                  | mark the callee `HW`, or inline it                                         |
| E405 | error         | signal-valued local reassigned in a way that is not a plain conditional                              | use `?:` or one assignment per branch                                      |
| W001 | lint          | `int`-family is unbounded `Int`                                                                      | `intN_t` for fixed width                                                   |
| W002 | lint          | `Nat` subtraction truncates at 0                                                                     | `--unsigned=int` or guard                                                  |
| W003 | lint          | `long double` → `Float`                                                                              | —                                                                          |
| W004 | lint          | uninitialised variable zero-initialised                                                              | initialise                                                                 |
| W005 | lint          | division by zero is `0`                                                                              | guard                                                                      |
| W006 | lint          | `printf` width/precision ignored                                                                     | —                                                                          |
| W007 | lint          | `char` ↔ integer conversion                                                                          | `uint8_t`                                                                  |
| W008 | lint          | exact-width arithmetic, no `int` promotion                                                           | —                                                                          |
| W009 | lint          | `Int.toNat` clamps negative values                                                                   | —                                                                          |
| W010 | lint          | `a[i]!` can panic                                                                                    | `--index=proof`                                                            |
| W011 | lint          | string indexing is O(n)                                                                              | `Array Char`                                                               |
| W012 | warning       | verification domain > 64 bits                                                                        | split                                                                      |
| W013 | warning       | `sorry` emitted by `--prove=none`                                                                    | —                                                                          |
| H001 | hint          | `bool a[N]`                                                                                          | `bv(N) a;`                                                                 |
| H002 | hint          | loop is not full-domain                                                                              | `VERIFY_FORALL`                                                            |
| H003 | hint          | `unsigned` where exact width intended                                                                | `uintN_t`                                                                  |
| H004 | hint          | bitwise on `char`                                                                                    | `uint8_t`                                                                  |

### 13.3 `synth` profile summary

Allowed: `bool`, `bv(N)`, `intN_t`/`uintN_t`, `enum`, `struct` of allowed types, constant-size arrays, pure functions, constant-bound `for`, `if`, `switch`, `?:`, non-recursive calls. Everything else is **E2xx**. A function annotated `NOSYNTH` is exempt (testbenches, reference models).

---

## 14. Worked examples

### 14.1 Hello and file-scope evaluation

```c
void greet(int n) { printf("n = %d\n", n); }
printf("%d", 2 + 3);
```

```lean
-- c: hello.c:1
def greet (n : Int) : IO Unit := do
  IO.println s!"n = {n}"

#eval 2 + 3
```

### 14.2 Local mutation → `Id.run do`

```c
int sum_to(int n) {
  int s = 0;
  for (int i = 1; i <= n; i++) s += i;
  return s;
}
```

The bound `n` is `Int`, so range-for does not apply (§6.1) and the general form is used:

```lean
-- c: sum.c:1
def sum_to (n : Int) : Int := Id.run do
  let mut s : Int := 0
  let mut i : Int := 1
  while i <= n do
    s := s + i
    i := i + 1
  return s
```

With `uint32_t n` and a constant lower bound the range form is used:

```lean
def sum_to (n : UInt32) : UInt32 := Id.run do
  let mut s : UInt32 := 0
  for i in [1:n.toNat + 1] do
    s := s + UInt32.ofNat i
  return s
```

### 14.3 Structures, arrays, switch

```c
typedef struct { int x; int y; } P;
enum Op { ADD, SUB };
int apply(enum Op op, P p) {
  switch (op) { case ADD: return p.x + p.y;
                case SUB: return p.x - p.y; }
  return 0;
}
```

```lean
structure P where
  x : Int
  y : Int
  deriving Repr, BEq, Inhabited

inductive Op | ADD | SUB
  deriving Repr, DecidableEq, Inhabited
open Op

def apply (op : Op) (p : P) : Int :=
  match op with
  | ADD => p.x + p.y
  | SUB => p.x - p.y
```

(The unreachable trailing `return 0;` is dropped when the `match` is exhaustive.)

### 14.4 S-box equivalence against Sparkle modules

```c
#include "sparkle.h"     // pulls in lean.h; needed for HW
typedef bv(8) byte;

HW extern byte sboxHW(byte);      // Sparkle module, combinational
HW extern byte sbox_impl(byte);   // Verilog translated into a Sparkle module
extern byte sbox_spec(byte);      // pure Lean reference function

sbox_equiv:
for (int i = 0; i < 256; i++) { sboxHW(i) == sbox_impl(i); }

VERIFY_FORALL(byte a) { sboxHW(a) == sbox_spec(a); }
```

```lean
import Std.Tactic.BVDecide
import Sparkle
set_option autoImplicit false
open Sparkle.Core.Domain Sparkle.Core.Signal
variable {dom : DomainConfig}

abbrev byte := BitVec 8

theorem sbox_equiv : ∀ i : byte,
    (sboxHW (Signal.lit dom i)).atTime 0 = (sbox_impl (Signal.lit dom i)).atTime 0 := by
  intro i
  simp only [sboxHW, sbox_impl]      -- plus Signal unfolding lemmas (Appendix B)
  bv_decide

theorem sboxHW_eq_spec : ∀ a : byte,
    (sboxHW (Signal.lit dom a)).atTime 0 = sbox_spec a := by
  intro a
  simp only [sboxHW]
  bv_decide
```

With `--sparkle-cmds` the first theorem may instead be lowered to Sparkle's own equivalence command.

### 14.5 Rejected input

```c
int *p = malloc(8);
p[0] = 1;
```

```
E108  buf.c:1:10  dynamic memory is not translatable
      why:  Lean values are immutable; there is no heap model.
      fix:  declare `int a[8];` (an Array) and index it.
E101  buf.c:2:1   pointer indexing on `p`
      fix:  after the change above, `a[0] = 1;` is accepted.
```

---

## 15. Roadmap

| Version   | Content                                                                                                                                                                                                                                                                             |
| --------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **v0.1**  | Everything tagged ✅: types, expressions, `do` control flow, functions, value-level `extern`, `sparkle.h` tier S0 (`HW` externs for combinational modules), I/O, `VERIFY_FORALL`, diagnostics, FPIL ch. 1–2 coverage.                                                                |
| **v0.2**  | 🟡 items: loop-pattern equivalence, `HW_LAT` and `VERIFY_AT`, `Vector α n`, `Option`/`Except`, tuples/out-parameters, function-typed parameters, `GENERIC`, stdin/file I/O, `--index=proof`, symbolic widths via `abbrev`, tactic overrides, struct/array at the hardware boundary. |
| **v0.3**  | `sparkle.h` authoring tiers S1 (combinational `HW` bodies) and S2 (`REG`/`NEXT`), Sparkle command macros (§17), sequential specs (§15.1), `--sparkle-cmds` lowering, differential-testing harness, `mvcgen`-style specifications.                                                   |
| **v1.0**  | Stable diagnostics and output format; Verilog→Lean interop so the C-authored model and the translated RTL meet in one Lean project.                                                                                                                                                 |
| **Later** | 🔮 C++ subset (templates → polymorphism, overloading → type classes), tagged unions, Python front-end sharing the same Lean emitter.                                                                                                                                                |

### 15.1 Sequential models (sketch, not normative)

Sparkle's own pattern for stateful designs is a **pure state machine** (`nextState` plus output functions) proved first, then implemented with `Signal.loop`/`Signal.register`. A clocked block in C maps naturally to that pure step function:

```c
typedef struct { bv(8) acc; } S;
S step(S s, bv(8) in) { s.acc = s.acc + in; return s; }
```

```lean
def step (s : S) (input : BitVec 8) : S := { s with acc := s.acc + input }
```

The relation to a Signal implementation is a bounded or inductive refinement statement with explicit reset value and latency. Concrete syntax is deliberately not fixed yet.

---

## 16. Open questions

| #      | Question                                                                                                                                              | Why it matters                                                                              |
| ------ | ----------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- |
| **Q2** | Fix the clock domain with a file-level `variable {dom : DomainConfig}` (this spec), or let users pin a concrete domain (`#pragma lean domain Foo`)?   | Domain-polymorphic theorems are more general; a concrete domain can be simpler for tactics. |
| **Q3** | Hardware boundary shapes: how should C structs and arrays map to Sparkle's tuple bundles (`Signal dom (A × B × …)`, right-nested) and `HWVector α n`? | Needed for modules with several outputs.                                                    |
| **Q4** | Is `a[i]!` (panics) the right default, or should the translator reject out-of-range constant indices?                                                 | Affects proof friendliness.                                                                 |
| **Q5** | Syntax for polymorphism and explicit constraints (`GENERIC(T)`, `GENERIC(T: Add)`)?                                                                   | Tier B design.                                                                              |
| **Q6** | Should file-scope statements ("script mode") be allowed in ordinary `.c` files, or only inside a `[[lean::verify]]` function?                         | Parsability with standard C tools.                                                          |
| **Q7** | Is C-authored hardware (`HW` bodies with `REG`/`NEXT`, §17.4) wanted at all, or should `sparkle.h` stay at the boundary (tier S0 only)?               | Decides whether v0.3 includes tiers S1/S2.                                                  |
| **Q8** | Is non-blocking `NEXT(reg, expr)` (all updates land together at the clock edge) an acceptable C spelling for `Signal.reg` + `<~`?                     | Readability for RTL-minded users.                                                           |

(Resolved: v0.1's Q1/Q2 by your sugar decision; v0.1's Q3 by §7.2/§9; v0.2's Q1 (exact-width types map to Lean's wrapping types). The numbering Q2–Q6 above is v0.2's; Q1 is retired.)

---

## 17. The Sparkle layer: `sparkle.h`

### 17.1 Decision and feasibility (D15–D17)

**Decision.** Sparkle-specific syntax lives in its own header, `sparkle.h`. `lean.h` ("vanilla Lean") contains nothing about Sparkle.

Findings from the feasibility check:

| Question                                     | Finding                                                                                                                                                                                                                                                                                                                                                                                                  | Verdict                                                                                 |
| -------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| Can the C side separate dialects by header?  | Yes. The tool is parse-only on top of the standard preprocessor (§3.1), so it sees `#include <sparkle.h>`. C23 attribute namespaces (`[[sparkle::…]]` vs `[[lean::…]]`) keep the two name sets apart. Same pattern as `<math.h>`.                                                                                                                                                                        | Feasible, low risk                                                                      |
| Can the Lean side be separated the same way? | Yes. Sparkle is an ordinary Lake package: `import Sparkle`, `open Sparkle.Core.Domain`, `open Sparkle.Core.Signal`. A file that does not use it gets none of these, so vanilla output compiles with core Lean and `Std` only.                                                                                                                                                                            | Feasible                                                                                |
| Does Sparkle's surface map from C?           | **Boundary:** yes (`Signal dom α ≈ Nat → α`, `Signal.lit`, `atTime`; §7.2). **Combinational:** operators are overloaded on `Signal dom (BitVec n)`, so expression bodies carry over almost verbatim. **Sequential:** Sparkle's registered-circuit idiom is `Signal.circuit do let c ← Signal.reg 0#8; c <~ c + 1#8; return c`, which lines up one-to-one with a C "register + next-value" idiom (§17.4). | Boundary: yes. Bodies: feasible for a narrow pattern, to be verified (Appendix B 11–14) |
| Is Sparkle's API stable?                     | Young and moving: the Reservoir page shows the latest commit building on Lean v4.34.1 while the README still says 4.28.                                                                                                                                                                                                                                                                                  | Churn expected; the split **contains** it in one header and one emitter back-end        |

The main benefit of the split is the last row: when Sparkle changes, only `sparkle.h` and the Sparkle emitter change. Vanilla users are unaffected.

### 17.2 Layering rules

1. `sparkle.h` includes `lean.h`. `lean.h` never includes `sparkle.h`.
2. A Sparkle name used without the include is **E401** (the tool recognises the names, so the message is "add `#include <sparkle.h>`" rather than a bare "undeclared identifier").
3. Output imports and opens Sparkle (and declares `variable {dom : DomainConfig}`) only when the header is included **and** a Sparkle feature is used (§3.2).
4. The dialect is **per function**. Functions are vanilla unless marked `HW`; one file may mix both.
5. Vanilla functions may call `HW` functions (the lift rule, §17.5). `HW` bodies may call only other `HW` functions (**E404**), because a value-level function cannot consume streams.
6. `HW` bodies are checked against the `synth` subset (Appendix A) whatever the file's profile (D17). Vanilla functions keep the file's profile.

### 17.3 Header sketch (normative for names, not for spelling of attributes)

```c
/* sparkle.h — Sparkle layer; parse-only */
#include <lean.h>

/* ---- the one attribute: this function is a Sparkle module ---- */
#define HW            [[sparkle::module]]        /* combinational (latency 0) */
#define HW_LAT(n)     [[sparkle::module(n)]]     /* n-cycle pipeline: declared latency */

/* ---- authoring (tiers S1/S2, 🟡) ---- */
#define REG(T, r, init)  T r = (init)            /* a register named r, reset value init */
#define NEXT(r, e)       /* r's value in the next cycle is e (non-blocking) */
#define HW_MAP(f, s)     /* apply a pure BitVec function to a signal: Signal.map f s */

/* ---- verification (§9.5) ---- */
#define VERIFY_AT(cycles, latency)               /* bounded cycle check */
#define VERIFY_EQ(f, g)                          /* pass-through: #verify_eq f g */

/* ---- Sparkle commands as file-scope statements ---- */
#define HW_SYNTH(f)      /* #synthesizeVerilog f */
#define HW_SHOW(f)       /* #showVerilog f */
```

`HW extern …` is the boundary form (§7.2): the module is defined in Lean. `HW …{ body }` is the authoring form: the module is defined in C and translated (D16).

### 17.4 The function set, by tier

| Tier   | What                                                                           | C spelling                                      | Lean emitted                                                                          | Status |
| ------ | ------------------------------------------------------------------------------ | ----------------------------------------------- | ------------------------------------------------------------------------------------- | ------ |
| **S0** | Use a module defined in Lean or translated Verilog                             | `HW extern bv(8) f(bv(8));`                     | nothing for the definition; calls use the lift rule (§17.5)                           | ✅      |
| **S0** | Pipelined module                                                               | `HW_LAT(n) extern …`                            | lift reads at cycle `n`                                                               | 🟡     |
| **S0** | Equivalence / spec checks                                                      | loop pattern, `VERIFY_FORALL`, `VERIFY_AT`      | theorems of §9                                                                        | ✅/🟡   |
| **S0** | Sparkle commands                                                               | `HW_SYNTH(f);` `HW_SHOW(f);` `VERIFY_EQ(f, g);` | `#synthesizeVerilog f`, `#showVerilog f`, `#verify_eq f g`                            | 🟡     |
| **S1** | Combinational module in C                                                      | `HW bv(8) f(bv(8) a, …) { … }`                  | `[hardware_module] def f {dom} (a : Signal dom (BitVec 8)) … : Signal dom (BitVec 8)` | 🟡     |
| **S2** | Registers                                                                      | `REG(bv(8), r, 0);` `NEXT(r, e);`               | `Signal.circuit do let r ← Signal.reg 0#8 … r <~ e … return …`                        | 🟡     |
| **S3** | Memories, register files, bundles, feedback via `Signal.loop`, multiple clocks | —                                               | —                                                                                     | 🔮     |

**S1 body rules** (inside an `HW` function):

| C                                                                     | Lean                                        | Note                                                                  |
| --------------------------------------------------------------------- | ------------------------------------------- | --------------------------------------------------------------------- |
| `bv(N)` / `bool` parameter, local, return                             | `Signal dom (BitVec N)` / `Signal dom Bool` | type lifting; everything in the body is a signal                      |
| `BV(8, 0x63)`, integer literal in a `bv` context                      | `0x63#8`                                    | Sparkle writes `count + 1#8` the same way                             |
| `+ - * & \| ^ ~ << >>`                                                | same operators                              | Sparkle overloads them on signals                                     |
| `c ? a : b`; `if (c) x = a; else x = b;`                              | `Signal.mux c a b`                          | argument order to verify                                              |
| `T x = e;` (once)                                                     | `let x := e`                                | a second assignment is **E405** unless it is the `if/else` form above |
| constant-bound `for`                                                  | unrolled                                    | like the `synth` profile                                              |
| `bv_ext`, `bv_cat`, `bv_zext`, …                                      | `Signal.map` of the vanilla BitVec function | the n-ary lift needs the applicative form (Appendix B 13)             |
| `Int`, `Nat`, `Array`, `String`, floats, `printf`, recursion, `while` | ⛔ (E201–E208)                               | `synth` subset                                                        |

**S2 semantics** (what a C reader must know):

- `REG(T, r, init)` declares a register; reading `r` gives its **current-cycle** value.
- `NEXT(r, e)` sets its value for the **next** cycle. All `NEXT`s take effect together at the clock edge (non-blocking, like Verilog `<=`), so order does not matter. At most one `NEXT` per register (**E403**); a condition is written with `?:`; without a `NEXT`, a register holds.
- Reset is Sparkle's register initial value; no separate reset signal is modelled (🔮).

### 17.5 The lift rule and what it means for each tier

From vanilla C a module is called with ordinary values (D14):

```
f(x, y)   ⟹   (f (Signal.lit dom x) (Signal.lit dom y)).atTime L          L = 0 for HW, n for HW_LAT(n)
```

- This holds identically for `HW extern` and for `HW` defined in C.
- It is meaningful when the module's output settles to a function of held inputs: combinational modules and feed-forward pipelines.
- A module with **feedback state** (e.g. a counter) has no such value-level view. Calling it from vanilla C is **E304**. It is checked with `VERIFY_AT` or refined against a pure step function (§15.1).
- `HW_LAT(n)` is the user's claim. The tool cannot check it; a wrong `n` makes the generated theorem fail, which is the check.

### 17.6 Examples

```c
#include <sparkle.h>

HW bv(8) addsub(bv(8) a, bv(8) b, bool sub) { return sub ? a - b : a + b; }

HW_LAT(1) bv(8) mul_reg(bv(8) a, bv(8) b) {
    REG(bv(8), p, 0);
    NEXT(p, a * b);
    return p;
}

HW bv(8) counter(bool en) {              // stateful: no value-level view
    REG(bv(8), count, 0);
    NEXT(count, en ? count + 1 : count);
    return count;
}

bv(8) mul_spec(bv(8) a, bv(8) b) { return a * b; }      // vanilla pure spec

VERIFY_FORALL(bv(8) a, bv(8) b) { mul_reg(a, b) == mul_spec(a, b); }
```

```lean
import Std.Tactic.BVDecide
import Sparkle
set_option autoImplicit false
open Sparkle.Core.Domain Sparkle.Core.Signal
variable {dom : DomainConfig}

[hardware_module]    -- exact attribute spelling: Appendix B 11
def addsub (a b : Signal dom (BitVec 8)) (sub : Signal dom Bool) : Signal dom (BitVec 8) :=
  Signal.mux sub (a - b) (a + b)

[hardware_module]
def mul_reg (a b : Signal dom (BitVec 8)) : Signal dom (BitVec 8) :=
  Signal.circuit do
    let p ← Signal.reg 0#8
    p <~ a * b
    return p

[hardware_module]
def counter (en : Signal dom Bool) : Signal dom (BitVec 8) :=
  Signal.circuit do
    let count ← Signal.reg 0#8
    count <~ Signal.mux en (count + 1#8) count
    return count

def mul_spec (a b : BitVec 8) : BitVec 8 := a * b

theorem mul_reg_eq_spec : ∀ a b : BitVec 8,
    (mul_reg (Signal.lit dom a) (Signal.lit dom b)).atTime 1 = mul_spec a b := by
  intro a b
  simp only [mul_reg, mul_spec]      -- plus Signal/circuit unfolding lemmas (Appendix B 4, 12)
  bv_decide
```

### 17.7 Implementation shape (rough)

- Front-end and typed IR are shared. Two emitters sit behind them: **vanilla** (always on) and **Sparkle** (loaded only when `sparkle.h` is seen).
- All Sparkle names (`Signal.lit`, `atTime`, `Signal.mux`, `Signal.circuit`, `Signal.reg`, `<~`, command names, the attribute spelling) live in **one data table** used by the Sparkle emitter, so an API rename is a one-line change.
- A conformance test set compiles every spec example against the Sparkle version a project pins, which catches API drift early.

### 17.8 Not covered (🔮)

`Signal.loop` feedback, `Signal.memory`, `HWVector`, tuple bundles (Q3), multiple clock domains, reset/enable semantics beyond the initial value, simulation commands (`#sim`), and `#verify_emit`.

---

## Appendix A — Banned constructs at a glance

| Construct                                                            | `model` | `synth`              |
| -------------------------------------------------------------------- | ------- | -------------------- |
| Pointer arithmetic, `T **`, `void *`, pointer casts                  | ⛔       | ⛔                    |
| `malloc`/`free`, `union`, bit-fields, `goto`, `volatile`, inline asm | ⛔       | ⛔                    |
| Assignment as expression, comma operator, `++` in expression         | ⛔       | ⛔                    |
| Function pointers                                                    | ⛔ (🟡)  | ⛔                    |
| Mutable globals, `static` locals                                     | ⛔ (🟡)  | ⛔                    |
| Recursion                                                            | ✅       | ⛔                    |
| `while` / `do-while` / non-range `for`                               | ✅       | ⛔                    |
| `int`, `unsigned`, `long`, `size_t`                                  | ✅       | ⛔                    |
| `float`, `double`, `long double`                                     | ✅       | ⛔                    |
| `String`, `char *`, `printf` and friends                             | ✅       | ⛔ (unless `NOSYNTH`) |

`HW` function bodies (§17) are always checked against the `synth` column. In addition, inside an `HW` body, only `bv(N)` and `bool` signals, constant-bound `for`, and calls to other `HW` functions are allowed.

## Appendix B — Names and behaviours to verify before implementation

Not confirmed against Lean 4.35 or against Sparkle's current `main`:

1. **Toolchain mismatch.** Sparkle pins its own Lean toolchain (its sources mention 4.28 and a "v4.32.1 bump"; the Reservoir page lists v4.34.1 for the latest commit), which can differ from 4.35. Generated code must compile on the project's pinned toolchain.
2. `BitVec.setWidth` vs the older `BitVec.zeroExtend`; `signExtend`, `getLsbD`, `rotateLeft`/`rotateRight`, `sdiv` (§5.4). Confirmed present in Sparkle's source: `extractLsb'`, `slt`, `sle`, `ult`, `ule`, `sshiftRight`, `BitVec.ofNat`, `Array.replicate`, `Array.set!`, `x[i]!`, `for i in [a:b]`.
3. **`bv_decide` under `lake build`.** Sparkle's known-issues file reports hangs in compiled/`lake build` mode and success interactively; re-test on your toolchain (§9.2).
4. **Sparkle equivalence commands** (`#verify_eq`, `#verify_eq_at (cycles := N) (latency := L) impl spec`): argument shapes and the exact `Signal` simp lemma names (`Signal.val_*`) used to unfold circuits before `bv_decide`. Read `Sparkle/Verification/Equivalence.lean` before fixing the unfolding set (§9.2, §9.5).
5. `import Std.Tactic.BVDecide` as the import path (the module `Std.Tactic.BVDecide.Syntax` exists), and the `bv_decide` option that reduces `UIntN`/`IntN` goals to `BitVec`.
6. Range-loop spelling `[a:b]` / `[a:b:k]` versus any newer range notation in 4.35 (reference ch. *Basic Types → Ranges*, ch. *Iterators*).
7. `assert!` availability and semantics inside `do` (§12.5).
8. `String` API (`String.toInt!`, `String.length`, indexing, `Char.isAlphanum`, …); the string/position API has been evolving (§4.3, §12).
9. `BitVec.toHex` for `%x` formatting (§8.1).
10. FPIL section names beyond chapter level: the book's navigation is script-rendered and only chapter titles were machine-verified; section-level statements in §10 follow FPIL's published structure but should be re-checked against the 4.32.0-tested edition.
11. **Module attribute spelling** (§17): your examples write `[hardware_module]`; Lean's attribute syntax is normally `@[hardware_module]`. Confirm against Sparkle's source and whether the attribute takes arguments.
12. **`Signal.circuit` / `Signal.reg` / `<~`** (§17.4): only one example was confirmed (from the Reservoir page: `Signal.circuit do let count ← Signal.reg 0#8; count <~ count + 1#8; return count`). Check multiple registers, conditional updates, whether a circuit may call other `[hardware_module]` functions, and which unfolding lemmas make such circuits provable with `bv_decide`.
13. **Signal-level operator details** (§17.4): `Signal.mux` argument order; the spelling of comparisons on signals (and whether they yield `Signal dom Bool`); whether a bare `BitVec` literal mixes with a `Signal` operand in every operator; the applicative form for n-ary lifts of plain `BitVec` functions.
14. **What the synthesis elaborator accepts** (`#synthesizeVerilog`): the exact restriction list, to align the `HW` body subset (§17.4, Appendix A) with it.
