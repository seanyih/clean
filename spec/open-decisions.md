# Open decisions — waiting for the human

Everything in the project that still needs a call from the owner, in one
place. Agents may add rows and keep the references fresh; **only the human
decides.**

**When a decision is made:** the row moves to
[design-decisions.md](design-decisions.md) as the next free number (D18,
D19, …), and the main-spec sections it touches are updated in the same
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
| OD8  | **`/` and `%` on negative `Int`.** C truncates toward zero (`-7/2 = -3`, `-7%2 = -1`); Lean's `Int` floors (`-4`, `1`). Today the output silently differs from C for any negative operand.      | notes/toposort.md ("Open question for the human"), impl-spec.md "Known simplifications", main-spec §5.1, D4 | (a) keep Lean's `/` `%` (consistent with D4) and add an opt-in lint for the divergence; (b) emit `Int.tdiv` / `Int.tmod` (C-truncating; an exception to D4; verified on Lean 4.15, re-check on 4.35); (c) refuse `/`/`%` when an operand may be negative (needs the typing pass of OD9). The note leans toward (b); §5.1 as written implies (a). |
| OD9  | **Mixing `int` and `unsigned` in one expression.** Today it is translated as written and Lean's coercions carry it — fine while files are single-typed, wrong once types mix. §5.3 wants explicit conversions, which need an expression-typing pass. | notes/2026-10-17-slice-4-nat.md ("Open for the human"), impl-spec.md "Known simplifications", main-spec §5.3 | (a) build the typing pass and emit the §5.3 conversions; (b) refuse mixed-type expressions until then; (c) keep relying on Lean's coercions (and its type errors) — today's behaviour.                 |
| OD10 | **Permanent refusals and the E000 placeholder.** `E000` is documented as "not translated in the current slice", but the build also uses it for refusals that will never gain a feature: unknown name/callee, arity mismatch, missing return, unreachable code, duplicates, bare-block shadowing, comparison-as-value. | src/diag.h, impl-spec.md "Diagnostic codes", [error-codes.md](error-codes.md) (E000 note)                   | (a) give them their own catalogue block (e.g. E0xx) so every message has a stable number; (b) declare E000 their permanent home and document that.                                                     |

## Provisional decisions taken without asking (veto window open)

From notes/2026-10-16-slice-0.md. They stand unless you veto; confirm them
and they move to [design-decisions.md](design-decisions.md).

| What              | Decision                                                                             | Where                                              |
| ----------------- | ------------------------------------------------------------------------------------ | -------------------------------------------------- |
| Build files       | plain `Makefile` at the repo root, builds into `release/`.                            | notes/2026-10-16-slice-0.md #1                      |
| Prelude header    | `release/lean.h` ships the parse-only prelude; the tool never reads it.               | notes/2026-10-16-slice-0.md #2, main-spec §3.1      |
| Language          | the tool itself is C++17 (the project allows C or C++).                               | notes/2026-10-16-slice-0.md #3                      |
| Diagnostics       | one placeholder code `E000` until each feature's real catalogue code arrives.        | notes/2026-10-16-slice-0.md #4, src/diag.h          |
| Output order      | ~~source order, no topological sort~~ — superseded: slices 1–3 added `toposort.cpp`. | notes/2026-10-16-slice-0.md #5, notes/toposort.md   |

Suggested next slices are a plan, not decisions; they live in impl-spec.md.
