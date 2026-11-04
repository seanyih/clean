# *Functional Programming in Lean* — translated into Clean

This folder walks through the book
<https://lean-lang.org/functional_programming_in_lean/> chapter by chapter and
writes its code in **Clean**: C syntax that `release/clean` translates to Lean.
It is written for a reader who already programs in C/C++ and wants to see, for each
part of the book, **what Clean can already do** and **what is still missing**.

* Every **` ```c `** block is a Clean program that `release/clean` accepts.
  `check.py` proves it (113 blocks today).
* Every **` ```lean `** block is the book's *original* Lean for a part Clean cannot
  express yet. Those blocks are the gap, kept verbatim so the missing feature is
  visible. They are always under a **❌** heading or inside a **🟡** section that
  says what is lost.

## Legend

| Mark | Meaning |
| --- | --- |
| ✅ | translated; the C block is verified by `check.py` |
| 🟡 | partly translated: a C stand-in exists (named in the text), the original Lean is shown for the rest |
| ❌ | not translatable today; only the original Lean is given, with the gap ID |
| n/a | build tooling, compiler messages or runtime internals — no code to translate |
| `G<n>` | an entry in the [gap register](#gap-register) below |

Conventions inside the C blocks: each block is a separate file (`#include "lean.h"`
is implicit; a block marked `<!-- clean: continue -->` extends the one above it, and
`<!-- clean: skip -->` marks an illustration that Clean *rejects on purpose*, shown
with the reason). Struct values are built with brace initialisers; Lean's `Nat` is
`unsigned`, `Int` is `int`, `Float` is `double`, `String` is `const char *`, `Array`
is a C array (main-spec §4).

## The chapters

| File | Book chapter | C blocks | Verdict |
| --- | --- | ---: | --- |
| [01-getting-to-know-lean.md](01-getting-to-know-lean.md) | 1. Getting to Know Lean | 34 | ✅ expressions, functions, structures, enums, `switch`, recursion · ❌ polymorphism, `List`/`Option`/`Prod`/`Sum`, lambdas, string slices |
| [02-hello-world.md](02-hello-world.md) | 2. Hello, World! | 12 | ✅ `main`, printing, reading, `argc/argv`, files, `cat` · ❌ `IO` actions as values, `Option`-returning `fileStream` |
| [03-interlude-propositions-proofs-indexing.md](03-interlude-propositions-proofs-indexing.md) | Interlude: Propositions, Proofs, and Indexing | 10 | 🟡 `VERIFY_FORALL` for finite-width facts, `LEAN_DECL` for closed theorems · ❌ proofs as arguments, `xs[i]?` |
| [04-overloading-and-type-classes.md](04-overloading-and-type-classes.md) | 3. Overloading and Type Classes | 12 | 🟡 data and algorithms as structs and named functions · ❌ all of `class`/`instance`/coercions |
| [05-monads.md](05-monads.md) | 4. Monads | 8 | 🟡 early return / accumulator / explicit state (the patterns monads abstract) · ❌ `Monad`, `Option`/`Except`/`State` types, `Expr` |
| [06-functors-applicative-functors-and-monads.md](06-functors-applicative-functors-and-monads.md) | 5. Functors, Applicative Functors, and Monads | 8 | 🟡 struct embedding, an error-accumulating validator · ❌ the type-class hierarchy, universes |
| [07-monad-transformers.md](07-monad-transformers.md) | 6. Monad Transformers | 11 | 🟡 the programs (config, `getUserInfo`, `divFrontend`, `countLetters`, loops) · ❌ transformers, `ForM`/`ForIn`, directory walk |
| [08-programming-with-dependent-types.md](08-programming-with-dependent-types.md) | 7. Programming with Dependent Types | 7 | ❌ the type-level material · 🟡 array programs and `bv(N)` as the C-side dependent type |
| [09-interlude-tactics-induction-proofs.md](09-interlude-tactics-induction-proofs.md) | Interlude: Tactics, Induction, and Proofs | 4 | ❌ induction/`simp`/`grind` · 🟡 finite analogues by `VERIFY_FORALL` |
| [10-programming-proving-and-performance.md](10-programming-proving-and-performance.md) | 8. Programming, Proving, and Performance | 10 | ✅ tail recursion, arrays, sorting, in-place updates · ❌ the `List` proofs, `Fin`, `dbgTraceIfShared` |
| — | 9. Next Steps | 0 | n/a — a reading list, no code |

The two interludes sit where they do in the book (after chapter 2 and after
chapter 7).

## How to re-check

```bash
make                                  # builds release/clean
python3 manual/fp-in-lean/check.py    # translates every ```c block; non-zero exit on a rejection
```

`check.py` only proves that Clean **translates** each block. Lean itself is not
installed in the environment where this manual was written, so the generated
`.lean` files were not type-checked by Lean here; the repository's own end-to-end
tests (`make test`) do that for `test/cases` when `lean` is available.

---

## What is done

Everything a C programmer needs for the book's first two chapters and the
imperative half of the later ones: functions and recursion on `int`/`unsigned`/
`bool`/`double`/`char`/strings; `struct` (nested, positional and designated
initialisers, field update), `enum` + `switch`, `typedef`; arrays (read, write,
`D20` in-place writers, 2-D); `if`/`for`/`while`/`do`/`break`/`continue`/early
`return`; `printf`/`scanf`/`fgets`/`fgetc`/`FILE *`/`argc, argv`/`exit`;
`#pragma lean namespace`; `[[lean::partial]]`; raw Lean through `LEAN_DECL`,
`LEAN_STMT`, `LEAN_EXPR`; and Clean's own contributions: `bv(N)` and
`VERIFY_FORALL` (a generated `theorem` closed by `bv_decide`).

## Gap register

Each row is one missing capability, with the C-side workaround that exists today
(if any), where the chapters hit it, and its place in the Clean specification
(`spec/main-spec.md`, `spec/open-decisions.md`). They are ordered by how much of the
book depends on them (the number of references in this folder).

| ID | Missing | Workaround today | Blocks | Spec status |
| --- | --- | --- | --- | --- |
| **G20** | **Type classes**: `class`, `instance`, overloaded operators (`+` on `Pos`), `ToString`/`Repr`/`BEq`/`Hashable` instances, coercion classes (`Coe`, `CoeFun`, …), `deriving` beyond the automatic one | named functions (`Pos_plus`), explicit conversion functions | ch. 3 (all), 4, 5, 6, 8 | 🔮 C++ / §11.3 "user type classes and instances" |
| **G3** | **Recursive and tagged `inductive` types**: constructors with payloads, `Nat.succ n`, `List.cons`, `BinTree`, `Expr`, JSON | enums for payload-free constructors; a struct with a tag + all fields for a closed sum | ch. 1.5, 3, 4, 5, 8, tactics | 🔮 "tagged unions → `inductive` with payloads"; `union` is E104, pointers E101 |
| **G5** | **Function values**: lambdas (`fun`, `·`), higher-order functions, partial application/currying, function-typed fields and parameters, `IO` actions as values | one C function per use; pass a flag/`enum` and `switch` | ch. 1, 2, 3, 4, 6, 7 | 🟡 "function-typed parameters" (E107 today) |
| **G18** | **Proofs as values**: custom `theorem`s, evidence arguments (`ok : xs.length > 2`), refinement types `{x // p x}`, `Fin n` indices, propositions as datatypes | `assert(...)` (a run-time `assert!`), `VERIFY_FORALL` for finite domains, `LEAN_DECL` for raw theorems | interlude 1, ch. 3, 5, 7, 8 | ⛔ by design D11 ("generated theorems are the only proof content") |
| **G21** | **Higher-kinded polymorphism**: `Functor`/`Applicative`/`Monad`, `ReaderT`/`StateT`/`ExceptT`/`OptionT`, custom `do` monads | the underlying patterns as plain C (early return, struct accumulators, passing state) | ch. 4, 5, 6 | 🔮 "monad stacks and transformers" |
| **G27** | **Writing proofs**: tactic choice, `induction`, `simp`, `rw`, `grind`, `fun_induction`; overriding the generated script | `VERIFY_FORALL` runs a fixed script (`intro …; try simp only […]; all_goals bv_decide`); `LEAN_DECL` carries a hand-written proof | tactics interlude, ch. 8 | 🟡 `[[lean::proof("…")]]` and `--prove=none` specified (§9.6), not built |
| **G1** | **`Option` / `Except`**: a result that may be missing or an error with a message | a struct `{bool ok; T val;}` / `{ok, err, val}` and early `return` | ch. 1.6, 3.4, 4, 5, 6, 7.2, 8.3 | 🟡 v0.2 "`Option`/`Except` conventions for error-code returns" |
| **G7** | **Types that depend on values / type-level computation**: `Vect α n`, `Type`, universes, type families, `asType` pattern, typed queries | `bv(N)` (the width is in the type), fixed-size arrays with an explicit `n` | ch. 1.6, 5.5, 7 | 🔮 "dependent types and indexed families"; `Vector α n` 🟡 v0.2 |
| **G12** | **`List`** (and `Vector`): linked lists, `::`, `[]` patterns, list-based exercises | an `Array` plus a length, fixed-size array fields | ch. 1.6, 3.4, 4, 8 | D13 (every array is an `Array`); `List` 🟡 "for linked structures" |
| **G4** | **Polymorphism**: type parameters `{α : Type}`, constraints `[Ord α]`, `PPoint α` | one definition per concrete type | ch. 1.6, 3.2, 4.6, 8 | 🟡 v0.2 `GENERIC(T)` (OD4 still open) |
| **G2** | **Tuples / `Prod`** and multiple return values | a named struct | ch. 1.6, 4, 5 | 🟡 v0.2 "tuples/out-parameters beyond the single-array case" |
| **G8** | **String API**: slices (`drop`, `dropWhile`, `dropEndWhile`, `take`, `trim`), `String.push`, a char as a string, string concatenation as an *expression* | `strcat` as a statement, `strlen`, `s[i]`, `<ctype.h>` | ch. 1.7, 2.1, 6.2 | pointer arithmetic E101; string writes E101 (§4.3) |
| **G15** | **File-system API**: `pathExists`, `readDir`, `isDir`, `currentDir`, paths; `fopen` returning `NULL` as a value | `fopen`/`fclose`/`fgetc`/`fgets` (a failing `fopen` stops the program, lint W016) | ch. 2.4, 6.1 | `IO.FS` 🟡 (§8.1); `NULL` handling an open decision |
| **G24** | **Iteration protocol**: `for x in coll` over user types (`ForIn`/`ForM`), parallel `for x in xs, y in ys` | a C `for` with an index | ch. 6.4 | 🔮 (follows G20/G21) |
| **G22** | **Structure inheritance with shared parents** (`extends A, B` with a common ancestor) and flattened initialisers | embed the parent as a field; copy fields of the second parent by hand | ch. 5.1 | not specified |
| **G23** | **Default field values** (`large := size == Size.large`) | missing initialisers are zero; write the rest | ch. 5.1, 6.1 | not specified |
| **G19** | **Arrays of strings** written as `const char *a[]` / `char *a[]` | `typedef const char *Str; Str a[N];` | ch. 3, 7 | E101 today ("arrays of strings are not translated yet"); looks like a small parser fix |
| **G6** | **Struct-valued expressions**: compound literals `(Point){1.0, 2.0}` as arguments or return values | declare a variable first, then pass it | ch. 1.4 | not specified (parse error today) |
| **G14** | **Binding patterns**: destructuring `let (a, b) := …`, `if let`, or-patterns that bind variables, struct patterns that bind fields | field access (`p.z`), tests, `switch` | ch. 1.8, 4.6 | follows G2/G3 |
| **G17** | **Calling a user `IO` function at file scope** (`#eval countdown 3`) | only `printf`/`puts`/… become `#eval`; raw Lean via `LEAN_DECL("#eval …")` | ch. 2.5 | not specified |
| **G25** | **More of `<stdio.h>`**: `fflush`, `fread`/byte chunks, `stdout.flush` | `fgetc`/`fgets`/`putchar` | ch. 2.4, 6.4 | §12.6 lists only `scanf`/files |
| **G13** | **More than one `namespace` per file**, `open … in` | `#pragma lean namespace` once per file | ch. 1.8 | §3.2 (one namespace) |
| **G16** | **Multi-module projects** (`import`, lakefiles) | one `.c` → one `.lean`; `#include` of another `.c` | ch. 2.3 | n/a for v0.1 |
| **G9** | `def` vs `abbrev` for type synonyms | `typedef` is always `abbrev` (so the failing `OfNat NaturalNumber 38` case cannot occur) | ch. 1.3 | by design |
| **G10** | Custom constructor names (`point ::`) and dot-call method syntax (`p.f x`) | plain functions with a type-prefixed name | ch. 1.4 | by design (C has neither) |
| **G11** | Pattern variables such as `Nat.succ k`, `n + 2` | `n - 1`, `n - 2` after the test | ch. 1.5, 1.8 | by design |
| **G26** | Type-error demonstrations (`#check` that fails) | Clean reports its own errors (`spec/error-codes.md`) | ch. 1.2 | n/a |
| **G28** | Debug and reflection commands (`dbgTraceIfShared`, `#print`, `#check` on arbitrary terms) | `LEAN_DECL("#check …")`, `LEAN_STMT` | ch. 4.5, 8.6 | covered by the raw-Lean escape hatch |

### Reading the register

* **Largest block, G20 + G21 + G3 + G5.** Type classes, higher-kinded polymorphism,
  tagged unions and function values together account for almost all of chapters
  3–6; none of them has a C spelling, so they are the long-term 🔮 items rather
  than quick wins. Chapters 3–6 are still *mostly* readable in C because their
  examples are ordinary programs once the abstraction is dropped.
* **Cheapest wins** (small, local changes to the translator): **G19** (accept
  `const char *a[]`), **G6** (compound literals), **G17** (file-scope calls of
  `IO` functions), **G25** (`fflush`), **G23** (default field values for partial
  initialisers), **G8** (a few string helpers such as a trim and a char-to-string).
* **Already planned** (v0.2, per main-spec §15): **G1** `Option`/`Except`,
  **G2** tuples and out-parameters, **G4** `GENERIC(T)`, **G5** function-typed
  parameters, **G12** `Vector`, **G27** tactic overrides. Implementing G1+G2+G5
  would unlock most of chapters 1.6, 4 and the "stand-in struct" workarounds in 5–7.
* **By design, never** (D5, D11): G9, G10, G11, G18, G26.

## A cheat sheet for the C reader

| Lean (book) | Clean (C) |
| --- | --- |
| `def f (a b : Int) : Int := a + b` | `int f(int a, int b) { return a + b; }` |
| `Nat` / `Int` / `Float` / `String` / `Bool` / `Char` | `unsigned` / `int` / `double` / `const char *` / `bool` / `char` |
| `structure P where x : Float y : Float` | `typedef struct { double x; double y; } P;` |
| `{ p with x := 0 }` | `p.x = 0;` (on a local copy) |
| `inductive Color \| red \| green` | `enum Color { red, green };` |
| `match n with \| 0 => … \| _ => …` | `switch (n) { case 0: …; default: … }` |
| `let mut x := 0; for i in [0:n] do x := x + i` | `unsigned x = 0; for (unsigned i = 0; i < n; i++) x += i;` |
| `#eval f 3` | `printf("%d", f(3));` at file scope |
| `def main : IO Unit := IO.println "hi"` | `int main(void) { puts("hi"); return 0; }` |
| `#[1, 2, 3]`, `a[i]!`, `a.set! i v` | `int a[3] = {1, 2, 3};`, `a[i]`, `a[i] = v;` |
| `partial def` | `[[lean::partial]]` |
| `theorem t : ∀ x : BitVec 8, … := by bv_decide` | `t: VERIFY_FORALL(bv(8) x) { … }` |
| any Lean | `LEAN_DECL("…")`, `LEAN_STMT("…")`, `LEAN_EXPR("…")` |

## Where this came from

The book's pages (chapters 1–9 and both interludes) were read from the live site and
their code blocks extracted; each C translation was then written and passed through
`release/clean`. The Lean in the ❌ sections was compared line by line against the
extracted book text to catch typos. Lines that do not appear there are my own
annotations (comments starting `-- `, such as `-- error: …` summarising the error
the book shows), proof terms the book renders interactively (`by decide`), and, in
[05-monads.md](05-monads.md) §4.4, the translator's own output. Where the book
walks through many intermediate versions of one definition or proof, only the
final (or the representative) version is reproduced. Section titles follow the
book's numbering.
