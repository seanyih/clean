# How to extend `clean` — a developer's manual

For downstream developers: where a feature lives, which files to touch, and
how to prove it works. State: slices 0–4 (2026-10-17). If the architecture
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
    check.cpp     AST → AST         names, arity, scopes, tests-vs-numbers,
      │                              every path returns, switch rules, marks
      │                              which locals are `mut`
      ▼
    toposort.cpp  AST → AST         callees first; finds recursion / mutual groups
      │
      ▼
    emitter.cpp   AST → Lean text   (merged params, `-- c:` comments)

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
   in lexer.cpp / parser.cpp and each file's header comment.

## Recipe 1 — add a type to the type table (`int` → `Int`, `unsigned` → `Nat`, …)

The main-spec §4.1 table lands row by row. Slice 4 did exactly this for
`unsigned` → `Nat`; imitate it.

1. **parser.cpp, `parseCType()`** — accept the new C spelling and return the
   string the AST stores (e.g. `"unsigned"`). Multi-word spellings are
   consumed here too (see the `unsigned int` skip); refuse compound forms
   that are not in this slice yet (see the `unsigned <word>` refusal).
2. **parser.cpp, `kUntranslated`** — remove the word(s) so they stop being
   auto-refused with E000.
   **Trap:** `expectName()` rejects type words in *name* position using
   `int`/`unsigned` **plus** `kUntranslated`. A word that leaves
   `kUntranslated` must be added to that check by hand, or `int short f();`
   would parse `short` as a function name (slice 4 fixed this for
   `int int f()`, `unsigned unsigned x = 1;`).
3. **emitter.cpp, `leanType()`** — add C spelling → Lean type. This one
   function is the whole table: return types, parameters, locals and the
   assigned-parameter `let mut` all go through it, so nothing else in the
   emitter changes.
4. **ast.h** — no change (types are plain strings), but update the
   "T = `int` / `unsigned`" comments on `Function` and `Decl` so the next
   reader is not misled.
5. **check.cpp** — only if the new type changes what is legal: e.g. `bool`
   makes truthiness trivial; `bv(N)` adds width checks (E301/E302); mixed
   types need the expression-typing pass (open decision OD9).
6. **Prove it:** add `test/cases/<name>.c` + `.expected.lean` +
   `.checks.lean` (Lean `example`s, usually `by rfl`); keep a
   `test/cases-should-fail/` case for the spellings that are still refused
   (cf. `type_not_yet.c`). `make test`.
7. **impl-spec.md** — extend "Current state" and the types paragraph.

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

- `test/cases/` — must translate; output is diffed against
  `<name>.expected.lean`; `<name>.checks.lean` is appended and the whole
  file is type-checked with `lean`.
- `test/cases-should-fail/` — clean must exit 1 with a diagnostic.
- `test/cases-lean-rejects/` — clean must succeed and `lean` must then refuse
  (things only Lean can judge, e.g. unproven termination).
- Lean-dependent steps are skipped, with a notice, when `lean` is not on
  PATH.

## Where the specs live

- `main-spec.md` — the agreement (mapping, semantics). Do not edit without
  the human's approval.
- `impl-spec.md` — what the build does today; updated with every slice.
- `design-decisions.md` — decided (D-numbers). `open-decisions.md` — queued
  for the human.
- `error-codes.md` — the diagnostic catalogue with implementation status.
