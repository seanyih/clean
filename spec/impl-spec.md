# `clean` — implementation spec

Companion to `main-spec.md` (the agreement). This file says what the code
does **today**. It is updated whenever a slice lands.

Current state: **slice 0** — the smallest runnable C → Lean path.

## What runs today

A C file goes in, a Lean file comes out, Lean type-checks it. That's the slice.

Accepted (in `.c` input):

| C                                        | Lean                                |
| ---------------------------------------- | ----------------------------------- |
| `#include …`, `#pragma …` (any `#` line) | dropped                             |
| prototype `int f(int a);`                 | dropped (main-spec §7.1)            |
| `int f(int a, int b) { return e; }`      | `def f (a b : Int) : Int := e`      |
| `int f(void) { return e; }`              | `def f : Int := e`                  |
| `int f()` (empty params)                 | same                                |
| comments above a definition              | kept, as `--` lines                 |
| `42`, `0xFF`                             | same spelling                       |
| `+ - * / %` with C precedence, `( )`     | same (Lean agrees on precedence)    |
| `-x`                                     | `(-x)`                              |
| `f(a, b)` with `f` defined above         | `f a b`                             |

Not translated yet (each is a hard error with location + why + fix):
anything else. That includes, in rough landing order: statements beyond a
single `return` (locals, `if`, `for`, `while`, `switch`), types beyond `int`
(`unsigned` → `Nat`, `bool`, `bv(N)` …), `main` special-casing, I/O and
`printf`, arrays, structs, enums, `extern`, preprocessor expansion (`#define`
names are recorded; **using** one is an error, not a silent wrong name),
comments *inside* function bodies, Sparkle, theorems.

Diagnostic codes: this build uses the single placeholder **E000** = "not in
the current slice". Real catalogue codes (E101, W001, …) arrive together with
the features they guard.

## How the tool is put together

Four layers, one direction, no cleverness:

    main.cpp     driver — read input.c, run the layers, write output.lean
      │
      ▼
    lexer.cpp    text  → tokens      (# lines vanish, comments become tokens)
      │
      ▼
    parser.cpp   tokens → AST        + early checks: unknown names, arity
      │                             mismatches, Lean keywords as C names,
      │                             callee-defined-below-caller
      ▼
    emitter.cpp  AST   → Lean text  (merged params, `-- c:` comments)

- `ast.h` — the data both sides speak; `diag.h` — the error type and the
  §13.1 message format.
- Each source file's header comment repeats its layer and its inputs/outputs.
- The tool never reads `release/lean.h`; it exists so C tools can parse our
  input dialect (main-spec §3.1).

## Build / run / test

    make                # builds release/clean
    make test          # build + end-to-end suite
    release/clean foo.c              # → foo.lean
    release/clean foo.c -o bar.lean

The suite (`test/run_e2e.sh`, `make test`):

1. translate each `test/cases/*.c`;
2. diff against its `.expected.lean` golden file;
3. append its `.checks.lean` (Lean `example`s) and type-check the whole
   thing with `lean`. If Lean is installed (it is here: 4.35.0), a wrong
   translation cannot pass;
4. each `test/cases-should-fail/*.c` must exit 1 with a diagnostic.

## Main-spec decisions honored here

- **D3** idiomatic output: `def`, merged parameter groups, minimal parentheses.
- **D5** reject, never guess: unknown identifiers, macro uses, arity
  mismatches and out-of-order callees are caught *before* emission, so the
  emitted Lean has no name errors left to find.
- **D8** names preserved verbatim; a Lean keyword as a C name is a hard error.
- **D9** every definition carries `-- c: file:LINE`.
- **D10** permissive in, strict out: prototypes, `f()`, stray `;` accepted;
  refusals come with a why and a concrete fix.
- **§13.1** every message has the `code file:LINE:COL what / why / fix` shape.

## Known simplifications (deliberate, listed so they surprise no one)

- **No topological sort** (§3.2): output is source order, so a callee must
  sit above its caller. Violations are rejected with a move-it hint.
- **Comments inside function bodies are dropped**; they survive once bodies
  become real `do` blocks in the statements slice.
- **`main` is not special yet** (§7.1 says `IO UInt32`); today it is an
  ordinary `Int` function.
- **No macro expansion**: `#define`d names are rejected when used.
- **Block comments** are re-emitted as `--` lines rather than `/- -/`
  (content preserved; the `/- -/` spelling is a one-line change later).
- **Hex literals** are passed through verbatim (`0xFF` is also valid Lean);
  binary literals `0b…` are not lexed yet.

## Next slices (suggested, one e2e case each)

1. Bodies with locals and `if` → `Id.run do` (D7's middle row).
2. `unsigned` → `Nat` and `bool` → `Bool` (the §4.1 type table starts growing).
3. Range-`for` (§6.1) and `while`.
4. `printf` → `IO` (§8), which also brings the `main : IO UInt32` rule.
5. `bv(N)` and the §5.4 helpers — the hardware-flavoured core.
