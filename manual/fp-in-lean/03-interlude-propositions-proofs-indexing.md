# Interlude: Propositions, Proofs, and Indexing — in Clean

Book chapter: <https://lean-lang.org/functional_programming_in_lean/Interlude___-Propositions___-Proofs___-and-Indexing/>
Legend and gap IDs: see [README.md](README.md).

| Section | Status |
| --- | --- |
| Propositions and Proofs (`rfl`, `theorem`) | 🟡 only through `VERIFY_FORALL` (value-level, `bv_decide`) or raw Lean |
| Tactics (`decide`, `simp`) | 🟡 the generated script is fixed; custom tactics ❌ G18 |
| Connectives (`∧ ∨ ¬ →`) | 🟡 `&& \|\| !` inside a `VERIFY_FORALL` body |
| Evidence as Arguments (`ok : xs.length > 2`) | ❌ G18 |
| Indexing Without Evidence (`xs[i]!`, `xs[i]?`) | ✅ `a[i]` is `a[i]!`; `xs[i]?` ❌ G1 |
| Messages You May Meet | n/a (Lean diagnostics) |

The interlude explains Lean's *logic*: `Prop`, proofs as values, and why `xs[2]`
needs evidence that the index is in bounds. C has no counterpart for proofs as
values, so this chapter is mostly the original Lean (marked ❌). What Clean
**does** give a C author is a way to *state and prove* facts about functions on
fixed-width values: the `VERIFY_FORALL` macro (main-spec §9) generates a
`theorem` and proves it with `bv_decide` (a SAT solver whose certificate Lean
checks).

---

## Indexing

The book's list lookups `woodlandCritters[0]` become array reads. In C the
index is just an `int`; Clean emits `a[i]!`, which panics at run time when out of
range (lint W010) — the book's "Use `a[i]!`" option. Lean's *compile-time*
bounds proof (`woodlandCritters[3]` is rejected by the elaborator) does not
exist in C (**G18**).

```c
typedef const char *Str;                 /* an array of strings needs a typedef (G19) */

const Str woodlandCritters[3] = {"hedgehog", "deer", "snail"};

const Str hedgehog = woodlandCritters[0];
const Str deer     = woodlandCritters[1];
const Str snail    = woodlandCritters[2];
```

`#eval woodlandCritters[1]!`:

<!-- clean: continue -->
```c
printf("%s", woodlandCritters[1]);        /* "deer" */
```

### ❌ `xs[i]?` returns an `Option` (G1) and `xs[i]'h` needs a proof (G18)

```lean
def woodlandCritters : List String :=
  ["hedgehog", "deer", "snail"]

def hedgehog := woodlandCritters[0]
def deer := woodlandCritters[1]
def snail := woodlandCritters[2]

def oops := woodlandCritters[3]
-- error: failed to prove index is valid, possible solutions: …

def thirdOption (xs : List α) : Option α := xs[2]?

#eval thirdOption woodlandCritters      -- some "snail"
#eval thirdOption ["only", "two"]       -- none
```

The closest C is a bounds test written by hand, returning a sentinel
(🟡 — the caller must remember to check):

```c
int thirdOr(const int xs[], unsigned n, int dflt) {
  if (n > 2) return xs[2];
  return dflt;
}
```

---

## Propositions and Proofs

`1 + 1 = 2` is a `Prop`; a *theorem* is a name for evidence of one. In Clean
there are two ways to state a theorem.

### 1. Raw Lean at file scope — `LEAN_DECL` (🟡)

For closed facts with no variable, pass the Lean through verbatim:

```c
LEAN_DECL("theorem onePlusOneIsTwo : 1 + 1 = 2 := by decide");
LEAN_DECL("theorem addAndAppend : 1 + 1 = 2 ∧ \"Str\".append \"ing\" = \"String\" := by decide");
LEAN_DECL("theorem onePlusOneOrLessThan : 1 + 1 = 2 ∨ 3 < 5 := by decide");
LEAN_DECL("theorem notTwoEqualFive : ¬(1 + 1 = 5) := by decide");
```

`LEAN_DECL` is not read by Clean — Lean is its only judge — so this is
the escape hatch D1 promises, not a C feature.

### 2. `VERIFY_FORALL` — theorems about finite domains (✅)

A C function and a boolean claim about every `bv(N)`, `uintN_t`, `intN_t` or
`bool` input generate `theorem … : ∀ …` closed by `bv_decide`:

```c
bool isEven(bv(8) x) { return (x & BV(8, 1)) == BV(8, 0); }

doubleIsEven: VERIFY_FORALL(bv(8) x) { isEven(x + x); }
```

Restrictions (all explained in main-spec §9.3): unbounded `int`/`unsigned`
cannot be quantified over (`bv_decide` searches bits); a theorem with *no*
variable is a syntax error (`VERIFY_FORALL()` is not accepted — use `LEAN_DECL`
above); the body is one boolean expression.

```c
VERIFY_FORALL(uint8_t a, uint8_t b) { a + b == b + a; }
```

---

## Connectives

The book's table of connectives and their evidence:

| Connective | Lean syntax | C inside `VERIFY_FORALL` |
| --- | --- | --- |
| True / False | `True` / `False` | `true` / `false` |
| `A` and `B` | `A ∧ B` | `a && b` → `∧` |
| `A` or `B` | `A ∨ B` | `a \|\| b` → `∨` |
| not `A` | `¬A` | `!a` → `¬` |
| `A` implies `B` | `A → B` | `!a \|\| b` (the classical reading) |

The book's `andImpliesOr : A ∧ B → A ∨ B` for propositions `A`, `B` becomes, for
*boolean* `A`, `B` (where "implies" is `¬(a ∧ b) ∨ (a ∨ b)`):

```c
andImpliesOr: VERIFY_FORALL(bool a, bool b) { !(a && b) || a || b; }
```

Evidence by `And.intro` / `Or.inl` / pattern matching on proofs is **not**
expressible — Clean never writes proofs, it only calls `bv_decide` (G18):

```lean
theorem andImpliesOr : A ∧ B → A ∨ B :=
  fun andEvidence =>
    match andEvidence with
    | And.intro a b => Or.inl a

theorem onePlusOneOrLessThan : 1 + 1 = 2 ∨ 3 < 5 := by decide

theorem notTwoEqualFive : ¬(1 + 1 = 5) := by decide

theorem trueIsTrue : True := by decide

theorem trueOrFalse : True ∨ False := by decide

theorem falseImpliesTrue : False → True := by decide
```

---

## Evidence as arguments — ❌ (G18)

A Lean function can *require a proof* of its precondition, so the call is
statically safe:

```lean
def third (xs : List α) : α := xs[2]         -- error: failed to prove index is valid

def third (xs : List α) (ok : xs.length > 2) : α := xs[2]

#eval third woodlandCritters (by decide)
-- "snail"

#eval third ["rabbit"] (by decide)
-- error: Tactic `decide` proved that the proposition ["rabbit"].length > 2 is false

def unsafeThird (xs : List α) : α := xs[2]!  -- error: failed to synthesize Inhabited α
```

There is no way to take a proof as a C parameter. The nearest C idiom is a
run-time check (`assert(n > 2);`, which Clean maps to `assert!` — a panic, *not*
a proof obligation, main-spec §12.5):

```c
#include <assert.h>

typedef const char *Str;

Str third(Str xs[], unsigned n) {
  assert(n > 2);
  return xs[2];
}
```

Naming a proposition (`def OnePlusOneIsTwo : Prop := 1 + 1 = 2`) and the
distinction between `def` (opaque to `simp`/`decide`) and `abbrev` (unfolded) is
Lean-only:

```lean
def onePlusOneIsTwo : 1 + 1 = 2 := rfl

def onePlusOneIsFifteen : 1 + 1 = 15 := rfl      -- error: not a definitional equality

def OnePlusOneIsTwo : Prop := 1 + 1 = 2

theorem onePlusOneIsTwo : OnePlusOneIsTwo := rfl

theorem onePlusOneIsStillTwo : OnePlusOneIsTwo := by simp     -- error: simp made no progress

theorem onePlusOneIsStillTwo : OnePlusOneIsTwo := by decide   -- error: failed to reduce to true/false
```

### Exercises of the interlude

*Prove with `rfl`:* `2 + 3 = 5`, `15 - 8 = 7`, `"Hello, ".append "world" = "Hello, world"`,
`5 < 18`. *Prove with `decide`:* the same list. *Write a function that looks up
the fifth entry of a list, taking the evidence that the lookup is safe.*

The first two parts as closed theorems through the raw-Lean escape hatch:

```c
LEAN_DECL("theorem two_plus_three : 2 + 3 = 5 := by decide");
LEAN_DECL("theorem fifteen_minus_eight : 15 - 8 = 7 := by decide");
LEAN_DECL("theorem hello_world : \"Hello, \".append \"world\" = \"Hello, world\" := by decide");
LEAN_DECL("theorem five_lt_eighteen : 5 < 18 := by decide");
```

`fifth` with evidence is ❌ (G18); the run-time-checked C version:

```c
int fifth(const int xs[], unsigned n) {
  assert(n > 4);
  return xs[4];
}
```
