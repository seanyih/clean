# How to extend `clean` — a developer's manual

For downstream developers: where a feature lives, which files to touch, and
how to prove it works. State: slices 0–6 (2026-10-19). If the architecture
moves, update this file with your slice.

## The pipeline (src/)

    main.cpp      driver — read input.c, run the layers, write output.lean
      │
      ▼
    lexer.cpp     text → tokens     (# lines dropped, comments become tokens,
      │                              unsupported characters still become tokens,
      │                              so the parser can point at line:col)
      ▼
    parser.cpp    tokens → AST      syntax only; desugars `x += e` → `x = x + e`
      │                              and `x++;` → `x = x + 1`
      ▼
    check.cpp     AST → AST         names, arity, scopes, expression types
      │                              (Expr::type) and implicit conversions,
      │                              tests-vs-values, every path returns,
      │                              switch rules, marks which locals are `mut`
      ▼
    lint.cpp      AST → notes       only with --lint=c-semantics: "C would behave
      │                              differently here" (W-codes); never edits the AST
      ▼
    toposort.cpp  AST → AST         callees first; finds recursion / mutual groups
      │
      ▼
    emitter.cpp   AST → Lean text   (merged params, `-- c:` comments)

- `types.h/.cpp` — beside the pipeline, not in it: the **only** file that
  names a C type, a Lean type, or a rule about mixing/converting them. The
  parser, checker and emitter ask it and never hardcode a type.
- `ast.h` — the data all layers speak. `diag.h` — the error type and the
  main-spec §13.1 rendering. Everything is one direction, no cleverness.
- To refuse: `fail(…)` / `failHere(…)` (E000) or `failCode("E###", …)` in
  parser/check, `throw DiagError(…)` elsewhere; main renders it and exits 1.
- The tool never reads `release/lean.h`; it exists for C tooling (§3.1).

## House rules

1. Top-down, one slice at a time (CLAUDE.md): one feature = one new
   end-to-end case; keep the suite green.
2. No unit tests — end-to-end only (`make test`).
3. Reject, never guess (D5): every refusal says why and gives a fix.
4. Diagnostic codes come from the catalogue in
   [error-codes.md](error-codes.md) and arrive with their feature; until then
   the placeholder is E000.
5. Update [impl-spec.md](impl-spec.md) with every slice. main-spec.md is the
   agreement — if your change contradicts it, raise it with the human
   ([open-decisions.md](open-decisions.md)) instead of editing silently.
6. Keep the "what this build does" strings fresh: the `kSliceWhy` messages
   in lexer.cpp / parser.cpp, `supportedTypesHint()` in types.cpp, and each
   file's header comment.

## Recipe 1 — add a datatype

Everything about a type lives in `src/types.h` / `src/types.cpp`. The parser,
checker and emitter call it; none of them names a type, so most types need
no change outside those two files.

**Case 1: a new C spelling of an existing type** (`long`, `size_t`, a new
`intN_t` width…). One edit: `kSupported` (and the rule in `resolveTypeWords`).
Move the word out of `kRefused` if it was there. Done.

**Case 2: a new type** (what `bool`, `uint8_t`, `bv(N)`, `double`, `char`
each were). In `types.h` / `types.cpp`:

1. `Type::Kind` — add the kind (and use `width` if it is parametrised;
   `bv(N)` does).
2. `resolveTypeWords` — the C spelling → the kind. A spelling with arguments
   (`bv(8)`) is read by the parser (`Parser::parseCType`) and handed to a
   `makeXxx` function here.
3. Compile. `-Wswitch` now lists every `switch (t.kind)` that lacks the new
   kind. The ones to fill in:
   - `leanName`, `cName` — the Lean and C spellings;
   - `truthTest` — how a value becomes a test (main-spec §5.2);
   - `checkBinaryOp` / `checkUnaryOp` — which operators exist on it (bitwise
     on `Int` → E303, `%` on floats, …);
   - `unify` — the common type when it meets another (D19 rank rule:
     `Nat < Int < intN_t/uintN_t < bv(N) < Float32 < Float`); default: refused;
   - `checkCompare`, `zeroValue` — which comparisons exist, and the value
     that pads/defaults an array of it;
   - `flowKind` / `convert` — implicit conversions and casts (§5.3). `convert`
     writes the Lean for each (from, to) pair; a missing pair is refused with
     "cast through …". The checker inserts a `Cast` node wherever
     `flowKind`/`unify` says Convert, so the emitter never converts by itself.
4. If the type has literals, teach the lexer a token (`FloatLit`, `CharLit`),
   the parser an `Expr::Kind`, check.cpp `valueType` its type, emitter.cpp
   `emitExpr` its spelling. (`BvLit`, `FloatLit`, `CharLit`, `BoolLit` are
   the worked examples; integer literals need nothing because `Literal`
   adapts to any numeric type.)
5. Operators are printed with **Lean's** precedence (`leanLevel` in
   emitter.cpp), not C's. A new operator needs its level there and its row in
   `checkBinaryOp`.

**Prove it:** `test/cases/<name>.c` + `.expected.lean` (read it yourself
before committing it) + `.checks.lean` (`#guard`s that evaluate the
translated functions; plain `example … := by rfl` works for `Int`/`Nat`
only). Add `test/cases-should-fail/` files for each refusal you added. Then
update impl-spec.md (the types table) and error-codes.md.

**Where the next features hook in**

| Feature (main-spec)                  | Hook                                                                      |
| ------------------------------------ | ------------------------------------------------------------------------- |
| typedefs, structs, enums (§4.5)      | file-scope declarations in `TranslationUnit` + `toposort.cpp` ordering; a `Type::Kind` that carries a name (`Type` already has `args`; add a name field, update `operator==`) |
| loops (§6.1), `?:`                   | recipe 3 (statement) / a new `Expr::Kind`; loops need `mut` marking and a termination story in `emitFunction` |
| `printf` / `IO` (§8)                 | purity inference (D7) in `emitFunction`; makes `void` functions observable |
| `bv_*` helpers (§5.4)                | calls to known names in `check.cpp` `Call`; signatures in types.cpp       |
| `<string.h>` helpers (§12.3)         | same: known names in `Call`, `String` arguments                           |
| writer extensions (D20)              | `findWriters` in `check.cpp` (fixpoint + refusals), `Stmt::writeBack` and `wret` in `emitter.cpp` — a row write-back (`fill(m[1], n)`) or a tuple return slots in there |
| `switch` on `uint8_t`/`bv`/`char`    | lift the guard in `check.cpp` `switchStmt` once Lean accepts the patterns |

Done and worth reading as worked examples: arrays (`Type::arrayOf`,
`Expr::Kind::Index`/`ArrayLit`, `initList`, `declStmt` in check.cpp),
strings (`StringLit`, `String` rows in `unify`/`convert`), `void`
(`CallStmt`, `return;`), and the `void` array writers (`findWriters` in
check.cpp, `writerParam`/`writeBack` and the value-yielding `if`/`match`
paths in `emitter.cpp`).

**Lean name provenance.** The sandbox that wrote the slices up to `void` had
no Lean; the names from those slices (`Array.replicate`, `Array.set!`,
`String.toList`, the `let _ := f x` statement shape) and the slice-7 writer
shapes have since been verified by the owner running the whole suite on
Lean 4.35.0 (2026-10-19). If a `.checks.lean` ever fails on a new type, the
name list in impl-spec.md "Known simplifications" is where to
look first; fix it in `convert` / `leanName` / `truthTest`.

### Recipe 1b — add a lint

A lint says where C behaves differently from the Lean that clean emits
(D4, D18). It is opt-in (`--lint=c-semantics`), reports to stderr, never
changes the output and never fails the build.

1. Take the next free W-number in [error-codes.md](error-codes.md) (a lint
   outside the catalogue is a spec change: raise it with the human).
2. `src/lint.cpp`: add a check in `Linter::expr` (or `stmts` / `type`) that
   calls `report(code, loc, what, why, fix)`. The tree is already typed and
   already carries the implicit `Cast` nodes, so test `e.type`,
   `e.kids[i].type` and `Expr::Kind::Cast` rather than re-deriving anything.
   `report` keeps the first place per code and counts the rest.
3. Add the line to the table in lint.h, flip the status in error-codes.md.
4. Prove it: add the trigger to `test/cases-lint/c_semantics.c` and the
   expected stderr to `c_semantics.expected.lint`.

## Recipe 2 — extend the Lean keyword exclusion list

**parser.cpp, `kLeanKeywords`** — add the string. `checkName()` consults the
set at every declaration site (function names, parameters, locals), so one
addition covers all three; the message tells the user to rename in the C
source. Policy is D8: fail loudly, never rename. The set is deliberately
*not exhaustive* — grow it when a collision is reported.

Prove it: drop the keyword into a `test/cases-should-fail/` case
(`lean_keyword_name.c` uses `match`); the run must exit 1 with the
"reserved by Lean" diagnostic.

## Recipe 3 — add a new statement

1. **ast.h** — extend `Stmt::Kind` (and only the fields it needs; the tree
   grows with the feature, never "for later").
2. **parser.cpp** — build it in `parseStmt()` / `parseStmtList()`. Purely
   syntactic desugaring (like `x++` → `x = x + 1`) happens here, not in the
   emitter.
3. **check.cpp** — give it meaning in `Checker::stmts()`: scopes, `mut`
   marking, `returnsOnAllPaths`, anything the emitter would otherwise guess.
4. **emitter.cpp** — print it in `emitList()`. The one-`return`-vs-`Id.run do`
   choice is made in `emitFunction()`.
   The largest worked example is `switch`: `parseSwitch` / `switchStmt` /
   `emitSwitch` (including grouped labels → or-patterns, `default` last,
   fall-through refusal E105).
5. Tests + impl-spec as in recipe 1.

## Recipe 4 — adopt a diagnostic code

[error-codes.md](error-codes.md) is the catalogue plus a per-code status
column. When your feature's trigger becomes reachable, switch to
`failCode("E###", …)` (or `throw DiagError("E###", …)` outside
parser/check), give the message a why and a fix, and flip the code's status
column in the same change. Until then the placeholder is E000. Never warn
silently, never guess (D5).

## Build, run, test

    make                            # builds release/clean
    make test                       # build + end-to-end suite (see impl-spec for the count)
    release/clean foo.c             # → foo.lean
    release/clean foo.c -o bar.lean
    release/clean foo.c --lint=c-semantics   # also print the W-lints

- `test/cases/` — must translate; output is diffed against
  `<name>.expected.lean`; `<name>.checks.lean` is appended and the whole
  file is type-checked with `lean`.
- `test/cases-should-fail/` — clean must exit 1 with a diagnostic.
- `test/cases-lean-rejects/` — clean must succeed and `lean` must then refuse
  (things only Lean can judge, e.g. unproven termination).
- `test/cases-lint/` — run with `--lint=c-semantics`: stderr must equal
  `<name>.expected.lint` and the Lean output must equal a run without the
  flag. Normal cases must print no W-lint by default.
- Lean-dependent steps are skipped, with a notice, when `lean` is not on
  PATH.

## Where the specs live

- `main-spec.md` — the agreement (mapping, semantics). Do not edit without
  the human's approval.
- `impl-spec.md` — what the build does today; updated with every slice.
- `design-decisions.md` — decided (D-numbers). `open-decisions.md` — queued
  for the human.
- `error-codes.md` — the diagnostic catalogue with implementation status.
