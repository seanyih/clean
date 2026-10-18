# `clean` — implementation spec

Companion to `main-spec.md` (the agreement). This file says what the code
does **today**. It is updated whenever a slice lands.

Current state: **slices 0–5** — functions over every scalar type of
main-spec §4.1 (integers, `intN_t`/`uintN_t`, `bool`, `char`, `bv(N)`,
`float`/`double`) with locals, `if`, `switch` (on `int`/`unsigned`), casts
and recursion. Adding a type is one table row plus a few `case`s: see
[how-to-extend.md](how-to-extend.md).

## What runs today

A C file goes in, a Lean file comes out, Lean type-checks it.

**File level**

| C                                        | Lean                                       |
| ---------------------------------------- | ------------------------------------------ |
| `#include …`, `#pragma …` (any `#` line) | dropped                                    |
| prototype `int f(int a);`                | dropped (main-spec §7.1)                   |
| `int f(int a, int b) { … }`              | `def f (a b : Int) : Int := …`             |
| `unsigned f(unsigned a) { … }`          | `def f (a : Nat) : Nat := …`               |
| `int f(void)` / `int f()`                | `def f : Int := …`                         |
| `[[lean::partial]] int f(…) { … }`       | `partial def f …`                          |
| functions in any order                   | sorted, callees first (see below)          |
| `//` comments, anywhere C allows         | `--` lines (see **Comments** below)        |
| `/* */` comments, anywhere C allows      | `/- -/`, multi-line kept (see **Comments**) |

**Comments.** `//` → `--`, `/* */` → `/- -/` (multi-line kept; a block
comment containing `/-` or `-/` falls back to `--` lines, because Lean's
block comments nest). A comment never causes an error: the parser lifts the
comments out of the token stream before parsing, then re-attaches each one
by where it sat —

* above the statement (or definition) it introduces, or sits inside;
* a comment in a *header region* (inside the condition of an `if`/`switch`,
  in a signature) attaches to the whole `if` / `switch` / definition;
* a comment just before a closing `}` stays inside that block: end of a
  body, branch or case group (a comment above a stripped `break;` too);
* anything else lands on whatever follows it — the next case group,
  statement or definition; comments after the last definition end the file.

Nothing is dropped except comments around a stray `;`. The odd
in-signature spots (inside a parameter list, between a type and a name,
between `return` and its value …) are **deferred**: they parse fine, but the
comment lands somewhere plausible rather than in place — do not rely on the
spot. An unterminated `/*` is still an error (E000).

**Bodies.** A body that is one `return e;` becomes `def f … := e`. Any other
body becomes `Id.run do` (see "Known simplifications" for how this differs
from the main spec's D7 table):

| C                                    | Lean                                          |
| ------------------------------------ | --------------------------------------------- |
| `T x = e;` (any scalar type)         | `let x : T' := e` (`let mut` if reassigned)   |
| `x = e;`  `x += e;`  `x++;`          | `x := e`  `x := x + e`  `x := x + 1`          |
| assigning a parameter                | `let mut a : T' := a` at the top              |
| `if (c) … else if (d) … else …`      | `if c then … else if d then … else …`         |
| `switch (e) { case 0: case 1: … }`   | `match e with \| 0 \| 1 => …` (see below)     |
| `return e;`                          | `return e`                                    |
| `{ … }` (bare block)                 | flattened into its parent                     |

**Expressions.** Literals: `42`, `0xFF`, `0b1010` keep their spelling;
`1.5`, `2e3`, `.5` (a trailing `f` is dropped) are floating literals;
`'a'`, `'\n'` are `Char`; `true`/`false` are `Bool`; `BV(8, 0x63)` is
`0x63#8`. Operators `+ - * / %` and the bitwise `& | ^ ~ << >>` (→ `&&& |||
^^^ ~~~ <<< >>>`) follow C's parse tree. The output carries the parentheses
*Lean's* precedence needs, which differs from C's for shifts and bitwise
operators. `-x` → `(-x)`, `f(a, b)` → `f a b`, `(T)e` is a cast.

**Types** (main-spec §4.1, all of it except arrays/strings/`void`):

| C                                                         | Lean                          |
| --------------------------------------------------------- | ----------------------------- |
| `int`, `signed`, `short`, `long`, `long long` (any order) | `Int`                         |
| `unsigned …`, `size_t`                                    | `Nat`                         |
| `int8_t` … `int64_t`                                      | `Int8` … `Int64`              |
| `uint8_t` … `uint64_t`, `unsigned char`                   | `UInt8` … `UInt64`            |
| `bv(N)` (N a literal or `#define NAME <integer>`)         | `BitVec N`                    |
| `float`                                                   | `Float32`                     |
| `double`, `long double`                                   | `Float`                       |
| `bool`, `_Bool`                                           | `Bool`                        |
| `char`                                                    | `Char`                        |

Every expression has a type (`Expr::type`, set by the checker). Literals
adapt to the other operand. Operands of one operation must have **one type**
— `Int` with `Nat` is the single exception and gives `Int` (Lean coerces).
Anything else is refused (`uint8_t + int`, `char + 1`, `bv(8) + bv(4)` =
**E301**); the fix is a cast, because Lean has no implicit conversions.
Casts follow main-spec §5.3 (see `convert` in types.cpp for the exact Lean
spelling per pair). Implicit conversions are only these: number → `bool`
("non-zero", D10), integer literal → `char` (`Char.ofNat`), and `Nat` → `Int`.
Operators are checked per type: bitwise on `int` is **E303**, on `char`
**H004**, `%` on floats, `-` on unsigned types, and `~` on `unsigned` are
refused with a fix.

**Tests.** `< <= > >= == != && || !` are allowed where a test is expected
(an `if` condition, or inside another test) and become Lean propositions:
`==` → `=`, `!=` → `≠`, `&&` → `∧`, `||` → `∨`, `!` → `¬`; on floats `==`
and `!=` stay `==`/`!=` (no decidable `=`). A value used as a test means
"non-zero" (`x ≠ 0`, `x ≠ 0#8` on `bv(8)`, `x != 0.0` on floats; a `bool`
as is; a `char` is refused). Where a **`bool` is wanted** (initialiser,
assignment, argument, `return`) the same operators are `Bool` values:
`decide (a < b)`, `&&`, `||`, `!`. Anywhere else a comparison is refused: C
would make it 0 or 1, and clean does not guess a coercion.

**Switch.** The scrutinee is `int` or `unsigned` (match patterns on the other
types are unverified). Labels are integer literals (`3`, `-1`, `0x10`). Labels that
follow each other become one or-pattern. Each case ends with `break;` or a
`return`; `default` may sit anywhere and is emitted last; no `default` →
`| _ => pure ()`. Fall-through from a non-empty case is **E105**.

**Recursion.** A function that reaches itself is printed as it is.
Functions that call each other are grouped in one `mutual … end` block. Whether
Lean can prove termination is **Lean's call**, not ours: on failure the user
adds `[[lean::partial]]` (all or none inside one `mutual` group). Recursive
functions without the attribute carry a `-- note:` pointing at that fix.
On `Nat` Lean proves ordinary countdown recursion (`n - 1`, `n / 10`, self and
mutual) without any attribute — the `nat_*` cases rely on that; on `Int` it
cannot (see `cases-lean-rejects`).

Not translated yet (each is a hard error with location + why + fix):
loops (`for`, `while`, `do`, `continue`), `break` outside a switch case's end,
`?:`, uninitialised locals (`int x;`), `void`, strings (`char *`), arrays,
structs, enums, typedefs, the §5.4 bit-vector helpers (`bv_cat` …),
`switch` on the new types, `main` special-casing, I/O and `printf`, arrays, structs,
enums, `extern`, other attributes, preprocessor expansion (`#define` names
are recorded; **using** one is an error, not a silent wrong name), Sparkle,
theorems.

Diagnostic codes: **E103** (assignment as expression), **E105** (switch
fall-through), **E110** (`++`/`--` in an expression), **E301** (bit-width
mismatch), **E302** (bit-vector width not a constant), **E303** (bitwise on
`int`), **H004** (bitwise on `char`) follow the catalogue
in spec/error-codes.md, which also tracks which codes are implemented.
**E000** is the placeholder for everything else ("not in the current
build"); real codes arrive with the features they guard.

## How the tool is put together

Six layers, one direction, no cleverness. `types.cpp` sits beside them: the
one place that knows C type spellings, Lean type names, and how types mix,
convert and print as tests. The layers ask it; none of them names a type.

    main.cpp     driver — read input.c, run the layers, write output.lean
      │
      ▼
    lexer.cpp    text  → tokens      (# lines vanish, comments become tokens)
      │
      ▼
    parser.cpp   tokens → AST        syntax only; desugars `+=` and `x++;`
      │                              lifts comments out and re-attaches
      │                              them by position (they never parse)
      │
      ▼
    check.cpp    AST   → AST         names, arity, scopes, expression types
      │                              (Expr::type), implicit conversions,
      │                              tests-vs-values, every path returns,
      │                              switch rules; marks which locals are `mut`
      ▼
    toposort.cpp AST   → AST         callees first; marks recursion / mutual groups
      │
      ▼
    emitter.cpp  AST   → Lean text   (merged params, `-- c:` comments)

- `types.h` — `Type` plus: C words → type, type → Lean name, `unify` (type of
  `a + b`), per-type operator rules, `convert` (casts), `truthTest`.
- `ast.h` — the data all layers speak; `diag.h` — the error type and the
  §13.1 message format.
- Each source file's header comment repeats its layer and its inputs/outputs.
- The tool never reads `release/lean.h`; it exists so C tools can parse our
  input dialect (main-spec §3.1).

## Definition order

Lean wants a function defined before it is used; C does not. `toposort.cpp`
puts each function after everything it calls. Code that is already in a valid
order is not touched. Functions in a call cycle stay together, in source
order, and become a `mutual` block. Every definition keeps its
`-- c: file:LINE`, so you can still find it in the C file.

## Build / run / test

    make                # builds release/clean
    make test           # build + end-to-end suite
    release/clean foo.c              # → foo.lean
    release/clean foo.c -o bar.lean

The suite (`test/run_e2e.sh`, `make test`):

1. translate each `test/cases/*.c`;
2. diff against its `.expected.lean` golden file;
3. append its `.checks.lean` (Lean `example`s / `#guard`s) and type-check the
   whole thing with `lean`. A wrong translation cannot pass;
4. each `test/cases-should-fail/*.c` must exit 1 with a diagnostic;
5. each `test/cases-lean-rejects/*.c` must translate fine **and** be rejected
   by `lean` (code whose legality only Lean can judge: unproven termination).

Lean is needed for steps 3 and 5 (steps are skipped, with a notice, if
`lean` is missing). Last full run **with Lean 4.35.0** (2026-10-18, comment
slice): 45/45 passing, plus `run_e2e_hard.sh` green — the earlier "written
but not yet run" checks (`int_family`, `fixed_width`, `bool`, `bitvec`,
`float`, `char`) all type-checked.

## Main-spec decisions honored here

- **D3** idiomatic output: `def`, merged parameter groups, minimal parentheses.
- **D5** reject, never guess: unknown identifiers, macro uses, arity
  mismatches, missing returns, uninitialised locals and comparisons-as-numbers
  are caught *before* emission, so the emitted Lean has no name or scope
  errors left to find.
- **D8** names preserved verbatim; a Lean keyword as a C name is a hard error.
- **D9** every definition carries `-- c: file:LINE`.
- **D10** permissive in, strict out: prototypes, `f()`, stray `;`, braces
  optional on single-statement `if` bodies; refusals come with a why and a fix.
- **§13.1** every message has the `code file:LINE:COL what / why / fix` shape.

## Known simplifications (deliberate, listed so they surprise no one)

- **D7 is simplified:** every body beyond a single `return` is `Id.run do`,
  even when nothing is reassigned (D7 would print a pure expression). Always
  correct, just less pretty.
- **Bare `{ }` blocks are flattened**, because Lean's nested `do` is not a
  scope. A bare block whose local hides a visible variable is refused.
  (`if`/`switch` bodies are real scopes and may shadow freely.)
- **`/` and `%` on negative numbers differ from C** (see `notes/`): C gives
  `-7/2 = -3`, `-7%2 = -1`; Lean's `/` and `%` on `Int` give `-4` and `1`.
  Needs a decision — open decision OD8 in spec/open-decisions.md.
- **`main` is not special yet** (§7.1 says `IO UInt32`); today it is an
  ordinary `Int` function.
- **No macro expansion**: `#define`d names are rejected when used.
- **Comments:** every position C allows is kept (see **Comments** above);
  only comments around a stray `;` are dropped with it. In-signature spots
  are deferred — the text is preserved, but its exact spot is not promised.
  Block comments containing `/-` or `-/` fall back to `--` lines (Lean block
  comments nest).
- **Literals**: hex is passed through verbatim; `0B…` is written `0b…`.
  Octal and suffixes (`10u`) are not translated.
- **Recursion is judged by Lean:** clean does not try to prove termination,
  and E201 (recursion rejected in `synth`) does not exist yet.
- **Mixed types are refused, not converted** (open decision OD9): only
  `Int` with `Nat` mix freely. Main-spec §4.2 would insert `char`↔integer
  conversions by itself; clean asks for the cast (`(unsigned)c`) instead.
  Likewise there is no implicit `Int` → `Nat` (`unsigned x = intVar;` is
  refused; cast it).
- **Unverified Lean names.** Written from the main spec and Lean docs, never
  run: `Float32` and its conversions (`Float.toFloat32`, `Float32.toFloat`),
  `Float.toUInt8`/`toInt8`…, `IntN.ofInt`/`ofNat`/`toInt`, `Neg`/`Complement`
  on `IntN`, `BitVec.ofInt`, shifts with a literal amount on `BitVec`.
- **`switch` on `uint8_t`, `bv(N)`, `char`…** is refused: literal patterns on
  those types are unverified. Use `if`/`else if`.
- **Casts of `Int` to `uintN_t`** wrap (`UIntN.ofNat ((x % 2^N).toNat)`);
  `Int`→`Nat` clamps negatives to 0 (lint W009, no lint pass yet).
- **`bv(N)` widths** are literals or `#define NAME <integer>` only.

## Next slices (suggested, one e2e case each)

1. Range-`for` (§6.1) and `while`; `break`/`continue` inside them.
2. `printf` → `IO` (§8), `void`, and the `main : IO UInt32` rule.
3. The §5.4 bit-vector helpers (`bv_cat`, `bv_ext`, `bv_zext`, …) — they also
   give E301 its fix.
4. Arrays (§4.4), strings (§4.3), then structs / enums / typedefs (§4.5):
   these need file-scope declarations in the AST, which is the next
   structural step.
