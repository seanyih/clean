# `clean` — implementation spec

Companion to `main-spec.md` (the agreement). This file says what the code
does **today**. It is updated whenever a slice lands.

Current state: **slices 0–4** — integer-family functions (`int`, `unsigned`,
`long`, `short`, `size_t`, …) with locals, `if`, `switch` and recursion. The
type machinery is built to be extended: see `notes/adding-a-type.md`.

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
| `//` comments, anywhere                  | `--` lines, kept above what they precede   |
| `/* */` comments                         | `/- -/` (multi-line kept)                  |

**Bodies.** A body that is one `return e;` becomes `def f … := e`. Any other
body becomes `Id.run do` (see "Known simplifications" for how this differs
from the main spec's D7 table):

| C                                    | Lean                                          |
| ------------------------------------ | --------------------------------------------- |
| `T x = e;` (`int` / `unsigned`)      | `let x : Int / Nat := e` (`let mut` if reassigned) |
| `x = e;`  `x += e;`  `x++;`          | `x := e`  `x := x + e`  `x := x + 1`          |
| assigning a parameter                | `let mut a : Int / Nat := a` at the top       |
| `if (c) … else if (d) … else …`      | `if c then … else if d then … else …`         |
| `switch (e) { case 0: case 1: … }`   | `match e with \| 0 \| 1 => …` (see below)     |
| `return e;`                          | `return e`                                    |
| `{ … }` (bare block)                 | flattened into its parent                     |

**Expressions.** `42`, `0xFF`, `0b1010` keep their spelling; `+ - * / %` with
C precedence and `( )`; `-x` → `(-x)`; `f(a, b)` → `f a b`.

**Types.** Signed spellings (`int`, `signed`, `short`, `long`, `long long`,
any order, optional `int`) → `Int`; `unsigned …` and `size_t` → `Nat`
(main-spec §4.1), everywhere a type can appear: return type, parameters,
locals. Every expression gets a type (`Expr::type`, set by the checker; `Int`
mixed with `Nat` is `Int`), but nothing is rejected for mixing: it is
translated as written and left to Lean's coercions (D4; the explicit
conversions of §5.3 are not built yet).

**Tests.** `< <= > >= == != && || !` are allowed where a test is expected
(an `if` condition, or inside another test) and become Lean propositions:
`==` → `=`, `!=` → `≠`, `&&` → `∧`, `||` → `∨`, `!` → `¬`. A plain number
used as a test means "non-zero": `if (x)` → `if x ≠ 0 then`. A comparison
anywhere else (`return a < b;`) is refused: C would make it 0 or 1, and
clean does not guess a coercion.

**Switch.** Labels are integer literals (`3`, `-1`, `0x10`). Labels that
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
`?:`, uninitialised locals (`int x;`), types beyond `int` and `unsigned`
(`bool`, `char`, `bv(N)`, the fixed-width `intN_t`/`uintN_t`, `unsigned short`
and friends …), `main` special-casing, I/O and `printf`, arrays, structs,
enums, `extern`, other attributes, preprocessor expansion (`#define` names
are recorded; **using** one is an error, not a silent wrong name), Sparkle,
theorems.

Diagnostic codes: **E103** (assignment as expression), **E105** (switch
fall-through), **E110** (`++`/`--` in an expression) follow the catalogue
in spec/error-codes.md, which also tracks which codes are implemented.
**E000** is the placeholder for everything else ("not in the current
build"); real codes arrive with the features they guard.

## How the tool is put together

Six layers, one direction, no cleverness. `types.cpp` sits beside them: the
one place that knows C type spellings, Lean type names, and how types combine.
The layers ask it; none of them names a type.

    main.cpp     driver — read input.c, run the layers, write output.lean
      │
      ▼
    lexer.cpp    text  → tokens      (# lines vanish, comments become tokens)
      │
      ▼
    parser.cpp   tokens → AST        syntax only; desugars `+=` and `x++;`
      │
      ▼
    check.cpp    AST   → AST         names, arity, scopes, tests-vs-numbers,
      │                              every path returns, switch rules;
      │                              marks which locals are `mut`
      ▼
    toposort.cpp AST   → AST         callees first; marks recursion / mutual groups
      │
      ▼
    emitter.cpp  AST   → Lean text   (merged params, `-- c:` comments)

- `types.h` — `Type` plus: C words → type, type → Lean name, type of `a + b`,
  how a value of the type becomes a test.
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
`lean` is missing). Last full run (without Lean, golden and refusal checks only): 26/26 passing; the Lean checks of `int_family` are written but not yet run.

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
- **Comments** at the end of a block (nothing after them) are dropped.
  Block comments containing `/-` or `-/` fall back to `--` lines (Lean block
  comments nest).
- **Literals**: hex is passed through verbatim; `0B…` is written `0b…`.
  Octal and suffixes (`10u`) are not translated.
- **Recursion is judged by Lean:** clean does not try to prove termination,
  and E201 (recursion rejected in `synth`) does not exist yet.
- **Expressions are not type-checked.** `int` and `unsigned` may be mixed in
  one expression; the output is translated as written and relies on Lean's
  `Nat → Int` coercions (D4). A negative literal reaching an `unsigned`
  (`unsigned x = -1;`) becomes a Lean type error, not a `clean` diagnostic.

## Next slices (suggested, one e2e case each)

1. `bool` → `Bool` (slice 4 did `unsigned` → `Nat`, the first §4.1 row after
   `int`; with `Nat`, simple recursion passes without `partial`).
2. Range-`for` (§6.1) and `while`; `break`/`continue` inside them.
3. `printf` → `IO` (§8), which also brings the `main : IO UInt32` rule.
4. `bv(N)` and the §5.4 helpers — the hardware-flavoured core.
