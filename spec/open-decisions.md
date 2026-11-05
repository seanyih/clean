# Open decisions — waiting for the human

Everything in the project that still needs a call from the owner, in one
place. Agents may add rows and keep the references fresh; **only the human
decides.**

**When a decision is made:** the row moves to
[design-decisions.md](design-decisions.md) as the next free number (D21,
D22, …), and the main-spec sections it touches are updated in the same
change. Until then nothing here is normative — this file is a queue, not an
agreement.

## Decisions to make

| ID   | Problem                                                                                                                      | Where it is described                                                                                      | Options on the table                                                                                                                                                  |
| ---- | ---------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| OD1  | **Clock domain of Sparkle output.** Every generated file that uses Sparkle declares `variable {dom : DomainConfig}`, so theorems are domain-polymorphic.                                       | main-spec §3.2, §7.2.2, §9.2 (was §16 Q2)                                                                   | (a) keep the file-level `variable {dom}` (current spec); (b) `#pragma lean domain Foo` pins one concrete domain per file. (a) is more general; (b) can be easier for tactics.                            |
| OD2  | **Structs and arrays at the hardware boundary.** A Sparkle module with several outputs needs bundle shapes; C structs/arrays have no agreed mapping yet. Blocks multi-output modules.           | main-spec §7.2.2 (restriction), §17.8 (not covered) (was §16 Q3)                                            | (a) right-nested tuple bundles `Signal dom (A × B × …)`; (b) `HWVector α n`; (c) both, picked by the C shape.                                                          |
| OD3  | **Default for `a[i]`.** The spec emits `a[i]!`, which panics at run time when out of range. Affects proof friendliness. Arrays are not implemented yet — decide before that slice.              | main-spec §4.4, §10 (`--index=proof` row) (was §16 Q4)                                                       | (a) keep `a[i]!` (current spec); (b) the translator rejects constant out-of-range indices at translation time; (c) make `--index=proof` the default.                                                |
| OD4  | **Polymorphism spelling** for the `GENERIC(T)` proposal.                                                                       | main-spec §11.2 (was §16 Q5)                                                                               | (a) `GENERIC(T)`, constraints inferred from operators (`+` → `[Add T]`, `<` → `[Ord T]`); (b) `GENERIC(T: Add)`, constraints written out. Open sub-question: several type parameters.                 |
| OD5  | **Where "script mode" is allowed** — bare statements at file scope become `#eval` checks.                                        | main-spec §8.2 (was §16 Q6)                                                                                | (a) any `.c` file (current spec); (b) only inside a `[[lean::verify]]` context. (b) stays parsable with stricter standard-C tools.                                                                     |
| OD6  | **Is C-authored hardware wanted at all** — `HW` bodies with `REG`/`NEXT` (tiers S1/S2) — or does `sparkle.h` stay at the boundary (`HW extern` only, tier S0)? Decides whether v0.3 builds S1/S2. | main-spec §17.4, §15 (v0.3 row) (was §16 Q7)                                                                | (a) build S1/S2; (b) boundary only (S0), revisit later.                                                                                                              |
| OD7  | **`NEXT(reg, expr)` spelling.** Updates are non-blocking: all `NEXT`s land together at the clock edge, like Verilog `<=`. Moot if OD6 says no.                                                  | main-spec §17.4 (S2 semantics) (was §16 Q8)                                                                 | (a) accept `NEXT` as specced; (b) pick another spelling.                                                                                                              |
| OD10 | **Permanent refusals and the E000 placeholder.** `E000` is documented as "not translated in the current slice", but the build also uses it for refusals that will never gain a feature: unknown name/callee, arity mismatch, missing return, unreachable code, duplicates, bare-block shadowing, comparison-as-value. | src/diag.h, impl-spec.md "Diagnostic codes", [error-codes.md](error-codes.md) (E000 note)                   | (a) give them their own catalogue block (e.g. E0xx) so every message has a stable number; (b) declare E000 their permanent home and document that.                                                     |
| OD12 | **How to parse the proposed C++ front end** (type classes, overloading, constructors, sum types in C++ syntax). | [cpp-front-end-design.md](cpp-front-end-design.md) §12, §13 | (a) grow clean's own parser to the small subset; (b) libclang. |
| OD13 | **Sum-type spelling** for `inductive` types with payloads. | [cpp-front-end-design.md](cpp-front-end-design.md) §5.1 | (a) the C tagged union (`enum` tag + anonymous `union`, also in `.c`); (b) `std::variant`; (c) both. |
| OD14 | **Converting constructor → `OfNat`.** `Pos(unsigned n)` makes `Pos p = 7;` legal in C++. | [cpp-front-end-design.md](cpp-front-end-design.md) §3.2 | (a) `OfNat S n` for every `n`; (b) `OfNat S (n + 1)` when the body subtracts one; (c) only with `[[lean::literal]]`. |
| OD15 | **Default member initialisers in plain C** (`struct Config { bool useASCII = false; };` → Lean field defaults). | [cpp-front-end-design.md](cpp-front-end-design.md) §3.3 | (a) accept in `.c` files as an extension; (b) C++ files only. |
| OD16 | **Overloaded functions and associated types.** | [cpp-front-end-design.md](cpp-front-end-design.md) §6.4, §8 | Overloads: (a) clean resolves them and emits `S.f`; (b) Lean's overloaded identifiers. Associated type: (a) `using Out = void;` placeholder; (b) an attribute. |

## Provisional decisions taken without asking (veto window open)

The first five rows are from notes/2026-10-16-slice-0.md; the rest were taken
while building strings, arrays, `void` and the lints. They stand unless you
veto; confirm them and they move to [design-decisions.md](design-decisions.md).

| What              | Decision                                                                             | Where                                              |
| ----------------- | ------------------------------------------------------------------------------------ | -------------------------------------------------- |
| Build files       | plain `Makefile` at the repo root, builds into `release/`.                            | notes/2026-10-16-slice-0.md #1                      |
| Prelude header    | `release/lean.h` ships the parse-only prelude; the tool never reads it.               | notes/2026-10-16-slice-0.md #2, main-spec §3.1      |
| Language          | the tool itself is C++17 (the project allows C or C++).                               | notes/2026-10-16-slice-0.md #3                      |
| Diagnostics       | one placeholder code `E000` until each feature's real catalogue code arrives.        | notes/2026-10-16-slice-0.md #4, src/diag.h          |
| Output order      | ~~source order, no topological sort~~ — superseded: slices 1–3 added `toposort.cpp`. | notes/2026-10-16-slice-0.md #5, notes/toposort.md   |
| String `==` / `!=` | compare the **contents** (`=` / `≠` in Lean); C would compare addresses. `<`, `>` on strings are refused (that is `strcmp`, a string helper). | impl-spec.md "Strings" |
| Void call statements | `f(x);` for a `void` function is `let _ := f x` (a pure `Unit` value, discarded); a non-`void` result discarded as a statement is refused. | impl-spec.md "Void" |
| Short array initialisers | `int a[5] = {7, 8};` is `#[7, 8] ++ Array.replicate 3 0` (C zero-fills the rest); `{0}` is `Array.replicate N 0`. | impl-spec.md "Arrays" |
| Lint set | `--lint=c-semantics` implements W001, W002, W005, W007, W008, W009, W010, W011 and the new W014; W003, W004, W006 wait for their features. | src/lint.h |

Taken while building slice 9 (input and files, 2026-10-23); they stand unless you veto:

| What | Decision | Where |
| ---- | -------- | ----- |
| `scanf` reads whole words | Each conversion takes one whitespace-separated word and the whitespace after it is consumed; C would read only what the conversion accepts and leave the rest in the stream (`%d` on `1.5` gives 1 in C, nothing here). Lean's streams cannot push a character back, so exact C behaviour would need our own buffering layer. Reported by the provisional lint **W015**. | impl-spec.md "Input and files", src/lint.cpp |
| `fopen` failure | An error that stops the program, not `NULL`; `NULL` and stream comparisons are refused. Lint **W016**. Alternative: map to `IO (Option Stream)` and give `NULL` a meaning. | impl-spec.md, src/lint.cpp |
| New lint codes | W015 and W016 are new catalogue entries (the catalogue says a lint outside it is a spec change). Confirm, renumber or drop them. | spec/error-codes.md |
| `FILE *` is `IO.FS.Stream` | One stream type; `fclose` is `flush` (Lean closes a handle when it is dropped). | src/types.cpp |
| `argv[0]` | The path of the running Lean program (`IO.appPath`). | impl-spec.md |
| `exit(n)` status | Taken mod 256 as a `UInt8`, like C. | src/check.cpp |
| Helper `def`s in the output | `hexOf` and the new input helpers are printed at the top of each file that needs them; their names are reserved (a user function of that name is refused). Lean `String.asString` is deprecated in 4.35, so `hexOf` now uses `String.ofList`. | src/prelude.cpp |

Taken while building the 2026-11 feature commits (`?:` through the catalogue codes); they stand unless you veto:

| What | Decision | Where |
| ---- | -------- | ----- |
| Limits are literals | `INT32_MAX` etc. become their number (`2147483647`), not main-spec §12.2's `Int32.maxValue.toInt`: the same value, and as a literal it adapts to an `int32_t`/`uint8_t` context with no conversion (D19). | impl-spec.md "The pure C library", src/builtins.cpp |
| `strcmp` | A generated helper returning -1 / 0 / 1 (C's `int`), not main-spec §12.3's `compare` (an `Ordering`, which `strcmp(a, b) == 0` cannot use). | src/prelude.cpp |
| C truth values from the library | `isdigit`, `isnan`, … return a `bool`, so `int r = isdigit(c);` is refused like any `bool` → `int`. | impl-spec.md "The pure C library" |
| Library names vs user functions | A file that defines `min`, `sqrt`, … gets its own; only the bit-vector helpers' names (`bv_*`) are reserved. | src/check.cpp `stdCall` |
| Exhaustive enum `switch` | A `switch` with a case for every enumerator drops the statements after it (main-spec §14.3 drops the safety-net `return 0;`), silently. | src/check.cpp `stmts` |
| `VERIFY_FORALL` names | A C label names the theorem; else `f_eq_g` for `f(…) == g(…)`, else `verify_LINE`. Main-spec §9.4's `<common-prefix>_equiv` rule and §14.4's `sboxHW_eq_spec` example disagree with each other; this picks neither. | impl-spec.md "Theorems" |
| `VERIFY_FORALL` proof script | `intro …; try simp only [every definition reached, Id.run, pure, bind]; all_goals bv_decide`, with `linter.unusedSimpArgs` off for the theorem. | src/emitter.cpp `emitVerify` |
| `LEAN_DECL` placement | Above every function → after the types, before the functions; below one → at the end of the file. | impl-spec.md "Raw Lean" |
| Reserved names (D8) | The Lean 4.35 tokens `at`, `end`, `where`, `instance`, `class`, `calc`, … (probed one by one) join the keyword list. `at` was a field name in practice. | src/parser.cpp `kLeanKeywords` |
| One variable per declaration | `int a, b;` is refused with a fix (each declarator would be its own `let`); struct fields do allow `double w, h;`. | src/parser.cpp |
| Test hooks | `NAME.flags` (extra flags) and `NAME.before.lean` (the Lean side of `extern`s) in test/cases. | test/run_e2e.sh |

Taken while closing the cheap gaps of the book walkthrough (manual/fp-in-lean, 2026-11-05); they stand unless you veto:

| What | Decision | Where |
| ---- | -------- | ----- |
| Arrays of strings | `const char *a[N]` and `char *a[]` are `Array String` everywhere (constants, locals, parameters, fields), not only `argv`. `char **p` stays refused. | impl-spec.md "Strings", src/types.cpp `applyDeclarator` |
| Compound literals | `(struct P){…}` is accepted for struct types only, in any expression; the anonymous form is emitted with its type, `(⟨…⟩ : P)`, so it works where Lean cannot infer one (`(⟨1, 2⟩ : P).x`). | impl-spec.md "Structs", src/parser.cpp `parseUnary` |
| File-scope calls | `f(x);` at file scope is `#eval f x` (script mode, main-spec §8.2 extended from `printf` to any `void` call). OD5 (where script mode is allowed) applies to it too. | impl-spec.md "I/O", src/parser.cpp |
| `fflush`, `strcpy` | `fflush(f)` is `f.flush` (same statement as `fclose`); `strcpy(dst, src);` is `dst := src`, a statement like `strcat`. | impl-spec.md "Input and files", "The pure C library" |
| `VERIFY_FORALL` proof script | when a `do` block was unfolded, `all_goals first \| bv_decide \| ((repeat' split) <;> bv_decide)`; the split is tried only after `bv_decide` fails, so passing theorems keep the short path and a false claim still gets a counterexample. | impl-spec.md "Theorems", src/emitter.cpp `emitVerify` |

Suggested next slices are a plan, not decisions; they live in impl-spec.md.
