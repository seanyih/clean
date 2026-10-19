# `clean` — implementation spec

Companion to `main-spec.md` (the agreement). This file says what the code
does **today**. It is updated whenever a slice lands.

Current state: **slices 0–7** — functions over every type of main-spec §4.1
and §4.3–4.4 except structs/enums/typedefs: integers, `intN_t`/`uintN_t`,
`bool`, `char`, `bv(N)`, `float`/`double`, `void`, strings and arrays, with
locals, `if`, `switch` (on `int`/`unsigned`), casts, automatic conversions
between mixed types (D19), recursion, the opt-in `--lint=c-semantics`
(D18), and `void` array writers (OD11 c → D20). Adding a type is one table
row plus a few `case`s: see
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
| `void f(…) { … }`                        | `def f … : Unit := …`                      |
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
| `T a[N] = {…};`  `T a[N];`           | `let a : Array T := #[…]` / `Array.replicate N …` |
| `a[i] = e;`  `a[i][j] += e;`         | `a := a.set! i e`  `a := a.set! i (a[i]!.set! j (a[i]![j]! + e))` |
| `f(x);` (`f` is `void`)              | `let _ := f x` (a writer: `x := f x …`, D20)   |
| `return;` (in a `void` function)     | `return ()` (in a writer: `return a`)          |
| assigning a parameter                | `let mut a : T' := a` at the top              |
| `if (c) … else if (d) … else …`      | `if c then … else if d then … else …`         |
| `switch (e) { case 0: case 1: … }`   | `match e with \| 0 \| 1 => …` (see below)     |
| `return e;`                          | `return e`                                    |
| `{ … }` (bare block)                 | flattened into its parent                     |

**Expressions.** Literals: `42`, `0xFF`, `0b1010` keep their spelling;
`1.5`, `2e3`, `.5` (a trailing `f` is dropped) are floating literals;
`'a'`, `'\n'` are `Char`; `"text"` is a `String` (escapes re-spelled for
Lean, adjacent literals join); `true`/`false` are `Bool`; `BV(8, 0x63)` is
`0x63#8`. Operators `+ - * / %` and the bitwise `& | ^ ~ << >>` (→ `&&& |||
^^^ ~~~ <<< >>>`) follow C's parse tree. The output carries the parentheses
*Lean's* precedence needs, which differs from C's for shifts and bitwise
operators. `-x` → `(-x)`, `f(a, b)` → `f a b`, `(T)e` is a cast, `a[i]` is
`a[i]!` (`s[i]` on a string is `s.toList[i]!`).

**Types** (main-spec §4.1, §4.3, §4.4; structs/enums/typedefs are not built):

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
| `char *`, `const char *`, `char s[]`                      | `String`                      |
| `T a[N]`, `T a[]`, `T *a` + length (parameter)            | `Array T`                     |
| `T a[M][N]`, `T m[][N]`                                   | `Array (Array T)`             |
| `void` (a return type)                                    | `Unit`                        |

`const` and `restrict` are ignored (§4.7). Every expression has a type
(`Expr::type`, set by the checker).

**Mixed types (D19).** When two operands differ, the conversion of main-spec
§5.3 is inserted for the one of lower rank, so the output always shows it:
`Nat < Int < intN_t/uintN_t < bv(N) < Float32 < Float`. Two fixed widths: the
wider one wins, at equal width the unsigned one. A literal takes the other
operand's type. `char` is its code (`Nat`) in arithmetic and two `char`s
compared stay `Char` (§4.2). In a shift, the result has the left operand's
type and the count converts to it. The same conversions apply to assignments,
arguments, `return` and array indices (an index is always a `Nat`: an `int`
index becomes `i.toNat`). Examples: `long + unsigned` → `a + (↑b : Int)`,
`uint8_t + int` → `a + UInt8.ofNat ((b % 256).toNat)`, `int * 1.5` →
`Float.ofInt x * 1.5`, `c - '0'` → `c.toNat - '0'.toNat`; see `convert` in
types.cpp for every pair. Not converted (an error with a fix): `bv(N)` with
`bv(M)` (**E301**), a `bool`, string or array with anything else, a negative
literal for an unsigned type. `(T)e` casts follow the same table, and refuse
the pairs without a row.
Operators are checked per type: bitwise on `int` is **E303**, on `char`
**H004**, `%` on floats, `-` on unsigned types and `~` on `unsigned` are
refused with a fix.

**Tests.** `< <= > >= == != && || !` are allowed where a test is expected
(an `if` condition, or inside another test) and become Lean propositions:
`==` → `=`, `!=` → `≠`, `&&` → `∧`, `||` → `∨`, `!` → `¬`; on floats `==`
and `!=` stay `==`/`!=` (no decidable `=`). A value used as a test means
"non-zero" (`x ≠ 0`, `x ≠ 0#8` on `bv(8)`, `x != 0.0` on floats, `s != ""`
on strings; a `bool` as is; a `char` or an array is refused). Where a
**`bool` is wanted** (initialiser, assignment, argument, `return`) the same
operators are `Bool` values: `decide (a < b)`, `&&`, `||`, `!`. Anywhere
else a comparison is refused: C would make it 0 or 1, and clean does not
guess a coercion.

**Void.** A `void` function is `: Unit`; `return;` is `return ()`, falling
off the end is fine, and the body of a non-trivial one ends in `pure ()`.
Functions are pure in Lean, so it can do nothing observable until `printf`
(§8) exists; it still translates, and `f(x);` is accepted for a `void` `f`
(as `let _ := f x`). The result of a `void` call used as a value, a `void`
variable or parameter, and a discarded non-`void` result are refused. The
one `void` function that does something observable is the **array writer**
(OD11 c → D20): it returns the modified array instead of `Unit`.

**Strings** (§4.3). `String` values: literals, `char *` / `char s[]`
parameters, locals initialised from a literal (`char s[] = "abc";`), indexing
(`s.toList[i]!`, lint W011). `==` and `!=` compare the contents; a string as a
test means "non-empty". Refused: writing through an index (**E101**, §4.3),
`+`, `<`, `>`, arrays of strings, `char a[N]` without a string literal. Not
built: the `<string.h>` helpers (`strlen`, `strcmp`, `strcat`, …).

**Arrays** (§4.4, D13: every C array is an `Array`; the size is not part of
the type). `T a[N] = {1, 2, 3}` is `#[1, 2, 3]`; a shorter list is padded like
C (`#[7, 8] ++ Array.replicate 3 0`); `{0}` is `Array.replicate N 0`; no
initialiser gives `N` default values; `a[] = {…}` takes its size from the list.
`T a[M][N]` nests. Sizes are literals or `#define NAME <integer>`. Element
types are any scalar or string; `bool a[N]` also prints hint **H001**
(silence with `#pragma lean allow_bool_array`). Parameters: `T a[]`, `T a[N]`,
`T m[][N]`, and `T *a` **only** when a length parameter follows it (§4.6,
**E101** otherwise); `T *p` anywhere else is **E101**. Writes (`a[i] = v`,
`+=`, `++`) work on **local** arrays and on **one array parameter of a `void`
function** — see "Array writers" below. Refused: `a = b`, `int a[n]` (variable
size), `a[0]`, too many initialisers, comparing arrays, brace elision
(`int m[2][2] = {1, 2, 3, 4}`).

**Array writers** (OD11 c → **D20**). C's `void fill(int a[], int n)`
changes the caller's array; Lean's arrays are values. A `void` function that
writes exactly one array parameter — directly or by passing it to another
writer — becomes a function that **returns the modified array**: the emitted
type is the parameter's instead of `Unit`, `return;` and every path that
falls off the end return `a` (an `if`/`switch` in last position gets a
`return a` on every path, including a synthesized missing `else`), and every
call statement re-assigns what was passed: `fill(x, n);` is `x := fill x n`
(a local is marked `let mut` for it). Passing your own array parameter to a
writer writes it too, so writer-ness flows backwards along call edges
(`findWriters` in check.cpp runs before the per-function checks). Refused:
a **non-`void`** function that writes an array parameter, **two or more**
written array parameters (both need a tuple return, not built), and a writer
call whose written argument is not a whole variable (`fill(m[1], n)` — copy
the row out and back). See `test/cases/array_writer.c` for every emitted
shape, verified on Lean 4.35.0.

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
`?:`, uninitialised scalar locals (`int x;`; arrays may be uninitialised),
structs, enums, typedefs, unions, `static`/`extern`/`inline`, the §5.4
bit-vector helpers (`bv_cat` …), the `<string.h>` helpers, `switch` on types
other than `int`/`unsigned`, `main` special-casing, I/O and `printf`, other
attributes, preprocessor expansion (`#define` names are recorded; **using**
one is an error, not a silent wrong name), Sparkle, theorems.

Diagnostic codes: **E103** (assignment as expression), **E105** (switch
fall-through), **E110** (`++`/`--` in an expression), **E301** (bit-width
mismatch), **E302** (bit-vector width not a constant), **E303** (bitwise on
`int`), **H004** (bitwise on `char`), **E101** (pointers outside §4.6, string
mutation), **H001** (`bool` array hint) follow the catalogue
in spec/error-codes.md, which also tracks which codes are implemented.
**E000** is the placeholder for everything else ("not in the current
build"); real codes arrive with the features they guard.

## How the tool is put together

Seven layers, one direction, no cleverness. `types.cpp` sits beside them: the
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
    lint.cpp     AST   → notes       only with --lint=c-semantics: where C behaves
      │                              differently (W001–W014); never changes the tree
      ▼
    toposort.cpp AST   → AST         callees first; marks recursion / mutual groups
      │
      ▼
    emitter.cpp  AST   → Lean text   (merged params, `-- c:` comments)

- `types.h` — `Type` plus: C words → type, declarators (`*`, `[N]`) → type,
  type → Lean name, `unify` (the common type of `a + b`), per-type operator
  rules, `flowKind`/`convert` (casts and implicit conversions), `truthTest`,
  `zeroValue`.
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
    release/clean foo.c --lint=c-semantics    # + W-lints on stderr (hints H… are always on stderr)

The suite (`test/run_e2e.sh`, `make test`):

1. translate each `test/cases/*.c`;
2. diff against its `.expected.lean` golden file;
3. append its `.checks.lean` (Lean `example`s / `#guard`s) and type-check the
   whole thing with `lean`. A wrong translation cannot pass;
4. each `test/cases-should-fail/*.c` must exit 1 with a diagnostic;
5. each `test/cases-lean-rejects/*.c` must translate fine **and** be rejected
   by `lean` (code whose legality only Lean can judge: unproven termination);
6. each `test/cases-lint/*.c` is run with `--lint=c-semantics`: stderr must
   equal its `.expected.lint`, and the Lean output must be identical to a run
   without the flag. Every `test/cases/` run also checks that no lint prints
   by default.

Lean is needed for steps 3 and 5 (steps are skipped, with a notice, if
`lean` is missing). Last full run here (2026-10-19, after the OD11 slice)
**with Lean 4.35.0**: all passing — golden diffs, refusals, lint reports and
the `.checks.lean` of every case including `array_writer` (77/77), plus
`run_e2e_hard.sh`.

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
- **D20** array writers: a `void` function that writes exactly one array
  parameter returns the modified array; call sites re-assign what they passed
  (see "Array writers" above).
- **§13.1** every message has the `code file:LINE:COL what / why / fix` shape.

## Known simplifications (deliberate, listed so they surprise no one)

- **D7 is simplified:** every body beyond a single `return` is `Id.run do`,
  even when nothing is reassigned (D7 would print a pure expression). Always
  correct, just less pretty.
- **Bare `{ }` blocks are flattened**, because Lean's nested `do` is not a
  scope. A bare block whose local hides a visible variable is refused.
  (`if`/`switch` bodies are real scopes and may shadow freely.)
- **`/` and `%` on negative `Int` differ from C** (decision D18): C gives
  `-7/2 = -3`, `-7%2 = -1`; Lean's `/` and `%` on `Int` give `-4` and `1`.
  clean keeps Lean's; `--lint=c-semantics` says where (W014). On `intN_t`
  Lean truncates like C, on `Nat` and `uintN_t` there is no difference.
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
- **Conversions follow D19, which is a choice, not C.** `uint8_t + int` is
  computed in 8 bits (the `int` is wrapped into a `UInt8`), `int + unsigned`
  in `Int`, `char - char` in `Nat` (so `'a' - 'b'` is 0, not -1). C's usual
  arithmetic conversions are not replicated. `--lint=c-semantics` flags the
  places where the difference shows (W002, W007, W008, W009).
- **Lean names, verified:** the slice-6 names (`Array.replicate`, `Array.set!`,
  `a[i]!` on nested arrays, `String.toList`, `#[…] ++ …`, `let _ := f x` as a
  statement, the `↑s.toList[i]!.toNat` shape) and the slice-7 writer shapes
  (`return a` at the end of a body and inside branches, a value-yielding
  `if`/`match` with a missing-`else` `return a` arm, `x := f x …` write-back)
  all type-checked on Lean 4.35.0 by the full suite (2026-10-19).
- **`switch` on `uint8_t`, `bv(N)`, `char`…** is refused: literal patterns on
  those types are unverified. Use `if`/`else if`.
- **Casts of `Int` to `uintN_t`** wrap (`UIntN.ofNat ((x % 2^N).toNat)`);
  `Int`→`Nat` clamps negatives to 0 (lint W009).
- **`bv(N)` widths** are literals or `#define NAME <integer>` only.

## Next slices (suggested, one e2e case each)

1. Range-`for` (§6.1) and `while`; `break`/`continue` inside them.
2. `printf` → `IO` (§8), which makes `void` functions useful and brings the
   `main : IO UInt32` rule.
3. Structs, enums, typedefs (§4.5): they need file-scope declarations in the
   AST, which is the next structural step. Also unlocks `switch` on enums.
4. The §5.4 bit-vector helpers (`bv_cat`, `bv_ext`, `bv_zext`, …) — they also
   give E301 its fix — and the `<string.h>` helpers (§12.3).
5. Writer extensions if wanted: `fill(m[i], n)` (write back into a row), and
   a tuple return for non-`void` writers / several written arrays.
