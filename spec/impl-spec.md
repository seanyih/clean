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
| `//` comments above a definition         | kept, as `--` lines                 |
| `/* */` comments above a definition      | kept, as `/- -/`                    |
| `42`, `0xFF`, `0b1010`                   | same spelling                       |
| `+ - * / %` with C precedence, `( )`     | same (Lean agrees on precedence)    |
| `-x`                                     | `(-x)`                              |
| `f(a, b)` with `f` defined anywhere      | `f a b`, and `f` is moved above     |
| definitions in any order                 | sorted: callees first (see below)   |

Not translated yet (each is a hard error with location + why + fix):
anything else. That includes, in rough landing order: statements beyond a
single `return` (locals, `if`, `for`, `while`, `switch`), types beyond `int`
(`unsigned` → `Nat`, `bool`, `bv(N)` …), `main` special-casing, I/O and
`printf`, arrays, structs, enums, `extern`, preprocessor expansion (`#define`
names are recorded; **using** one is an error, not a silent wrong name),
comments *inside* function bodies, recursion (`mutual`), Sparkle, theorems.

Diagnostic codes: this build uses the single placeholder **E000** = "not in
the current slice". Real catalogue codes (E101, W001, …) arrive together with
the features they guard.

## How the tool is put together

Five layers, one direction, no cleverness:

    main.cpp     driver — read input.c, run the layers, write output.lean
      │
      ▼
    lexer.cpp    text  → tokens      (# lines vanish, comments become tokens)
      │
      ▼
    parser.cpp   tokens → AST        + early checks: unknown names, arity
      │                             mismatches, Lean keywords as C names
      │
      ▼
    toposort.cpp AST   → AST        (callees first; recursion is an error)
      │
      ▼
    emitter.cpp  AST   → Lean text  (merged params, `-- c:` comments)

- `ast.h` — the data both sides speak; `diag.h` — the error type and the
  §13.1 message format.
- Each source file's header comment repeats its layer and its inputs/outputs.
- The tool never reads `release/lean.h`; it exists so C tools can parse our
  input dialect (main-spec §3.1).

## Definition order

Lean wants a function defined before it is used; C does not. `toposort.cpp`
fixes that: it walks the functions in source order and puts each one after
everything it calls. Code that is already in a valid order is not touched.
A function that (directly or not) calls itself is refused with its call
path (`ping → pong → ping`); `mutual … end` comes with the recursion slice.
Every definition keeps its `-- c: file:LINE`, so you can still find it in
the C file.

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
  mismatches and recursion are caught *before* emission, so the emitted Lean
  has no name errors left to find.
- **D8** names preserved verbatim; a Lean keyword as a C name is a hard error.
- **D9** every definition carries `-- c: file:LINE`.
- **D10** permissive in, strict out: prototypes, `f()`, stray `;` accepted;
  refusals come with a why and a concrete fix.
- **§13.1** every message has the `code file:LINE:COL what / why / fix` shape.

## Known simplifications (deliberate, listed so they surprise no one)

- **Recursion is rejected** (§3.2 says cycles become `mutual … end`): not
  built yet.
- **Comments inside function bodies are dropped**; they survive once bodies
  become real `do` blocks in the statements slice.
- **`main` is not special yet** (§7.1 says `IO UInt32`); today it is an
  ordinary `Int` function.
- **No macro expansion**: `#define`d names are rejected when used.
- **Block comments** that themselves contain `/-` or `-/` fall back to `--`
  lines (Lean block comments nest, so they would break the output).
- **Literals**: hex is passed through verbatim; `0B…` is written `0b…`.
  Octal and suffixes (`10u`) are not translated.

## Next slices (suggested, one e2e case each)

1. Bodies with locals and `if` → `Id.run do` (D7's middle row).
2. `unsigned` → `Nat` and `bool` → `Bool` (the §4.1 type table starts growing).
3. Range-`for` (§6.1) and `while`.
4. `printf` → `IO` (§8), which also brings the `main : IO UInt32` rule.
5. `bv(N)` and the §5.4 helpers — the hardware-flavoured core.
