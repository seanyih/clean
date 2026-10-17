# Design decisions — the register

The agreement-level decisions of `clean`, extracted from main-spec §2.
D1–D17 are quoted from there; every open question that gets decided
([open-decisions.md](open-decisions.md)) is appended after D17 as D18+.

- Section references (§…) point into main-spec.md.
- D-numbers are stable — main-spec text refers to them (D4, D14, …). Do not renumber.
- If main-spec and this file ever disagree, the human arbitrates.

| ID  | Decision                                                                                                                                                                                                         | Rationale                                                                                                                             |
| --- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| D1  | **One-way, C → Lean.** Not every Lean construct has a C spelling. Anything unreachable is covered by explicit passthrough macros (`LEAN_DECL`, `LEAN_STMT`, `LEAN_EXPR`).                                        | "All Lean syntax has a C counterpart" cannot hold (dependent types, tactics, universe levels). Passthrough keeps the tool open-ended. |
| D2  | C need not compile, but must parse. A prelude header (`lean.h`, §3.1) supplies `bv`, `BV`, `VERIFY_FORALL`, `LEAN_*`, `NOSYNTH`; Sparkle names come from `sparkle.h` (§17).                                        | Lets IDEs and a C front-end parse hardware-flavoured C without a build.                                                               |
| D3  | Output is **idiomatic Lean**: `def`, `do`, `let mut`, `for`, `match`. It is not an AST plus interpreter.                                                                                                          | Readable for review; matches what FPIL teaches.                                                                                       |
| D4  | **C syntax, Lean semantics.** The tool does not emulate C. Divergences from C behaviour are reported only by **opt-in lints** (`--lint=c-semantics`, codes W001–W011); they are off by default. Genuinely unsupported syntax is still an **error**. | Your stated intent: pure syntax sugar. Keeps normal output quiet while allowing a C-minded reviewer to audit.                         |
| D5  | **Reject, never guess.** Unsupported constructs are errors with a fix hint. No silent `sorry`/`axiom`.                                                                                                           | The translator is in the trusted base; silent wrong answers are worse than refusals.                                                  |
| D6  | Two profiles: `model` and `synth` (§0.2).                                                                                                                                                                        | Ground-truth models may use strings, floats, `printf`; hardware stand-ins may not.                                                    |
| D7  | **Purity is inferred**: pure → plain `def`; local mutation → `Id.run do`; I/O → `IO`.                                                                                                                            | Keeps pure functions pure, so they can appear in theorems.                                                                            |
| D8  | Identifiers are preserved verbatim; Lean keywords fails loudly.                                                                                                                                                  | Predictable names.                                                                                                                    |
| D9  | Every emitted declaration carries a traceability comment `-- c: file.c:LINE`.                                                                                                                                    | Review and debugging.                                                                                                                 |
| D10 | **Permissive input, strict diagnostics**: missing includes/prototypes, int-as-bool, implicit conversions are accepted; the tool inserts the explicit Lean form and explains on request.                           | "Friendly for C users to write."                                                                                                      |
| D11 | Theorems are generated only for the narrow equivalence pattern (§9).                                                                                                                                             | Scope control.                                                                                                                        |
| D12 | The translator is a trusted component. A differential-testing harness (run C-side model vs `#eval` of the output) is recommended but non-normative.                                                              | Mitigates translator bugs that Lean cannot catch.                                                                                     |
| D13 | v0.1 maps **all C arrays to `Array`**. `Vector α n` is opt-in (🟡).                                                                                                                                              | Matches FPIL, avoids index-proof friction.                                                                                            |
| D14 | **Hardware modules are `HW` functions (from `sparkle.h`).** From vanilla C they are called with plain `BitVec`/`Bool` values; the tool lifts a call into Sparkle's `Signal` world by driving constant streams and sampling at a declared latency (§7.2, §17.5). | `Signal dom α` is a time stream (`Nat → α`); C has no time. A fixed, explicit convention keeps the C side time-free.                  |
| D15 | **Vanilla Lean and Sparkle Lean are separated by header.** `lean.h` knows nothing of Sparkle; `sparkle.h` includes `lean.h` and adds the Sparkle names under the `[[sparkle::…]]` attribute namespace. Output imports Sparkle only when the header is included and used (§17.2). | Contains Sparkle API churn to one header plus one emitter back-end; vanilla output never depends on Sparkle.                          |
| D16 | **One attribute, `extern` decides where the module lives.** `HW extern T f(…);` = defined in Lean; `HW T f(…) { … }` = defined in C and translated (🟡).                                                                                         | One concept for the user; same call rule either way.                                                                                  |
| D17 | **`HW` bodies always obey the `synth` subset**, whatever the file's profile (§17.4).                                                                                                                             | A hardware body that is not synthesizable is a bug, not a model.                                                                      |

## Formerly open questions, already folded in

Recorded here so the numbering archaeology ("Q3 of which version?") stops
mattering.

| Was         | Decision                                                                                                                                                     | Folded into                              |
| ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------- |
| v0.1 Q1/Q2  | `int` → `Int`, `unsigned` → `Nat`: pure syntax sugar, no C-semantics emulation. Divergences surface as opt-in lints (W001, W002).                              | D4, main-spec §4.1                       |
| v0.2 Q1     | Exact-width types map to Lean's own wrapping types: `int8_t` → `Int8`, … , `uint64_t` → `UInt64`. Nothing is emulated.                                          | main-spec §4.1                           |
| v0.1 Q3     | Sparkle modules are read through `HW` externs plus the lift rule; §9.1 Option A is normative, Option B (`--sparkle-cmds`) an optional lowering.                | D14–D17, main-spec §7.2, §9              |

## How a decided question lands here

1. Delete its row from [open-decisions.md](open-decisions.md).
2. Append it as the next free D number with a one-line decision, its
   rationale, and a pointer to where the full discussion lives.
3. Update the main-spec sections the decision changes, in the same change —
   the main spec stays the agreement, this file is its decision log.
