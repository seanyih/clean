# Interlude: Tactics, Induction, and Proofs — in Clean

Book chapter: <https://lean-lang.org/functional_programming_in_lean/Interlude___-Tactics___-Induction___-and-Proofs/>
Legend and gap IDs: see [README.md](README.md).

This interlude teaches *proof writing*: the `induction` tactic, `simp`, `rw`, `grind`,
and the `<;>` combinator, applied to `plusR_zero_left` and `BinTree.mirror_count`.
Clean does not let a C author write proofs (design decision D11: "generated
theorems are the only proof content"). What it offers instead:

| Book | Clean |
| --- | --- |
| prove a fact about a *finite-width* function | ✅ `VERIFY_FORALL` → `theorem` closed by `bv_decide` (a SAT solver; Lean checks the certificate) |
| choose the tactic script | ❌ **G27** — the script is fixed: `intro …; try simp only [definitions]; all_goals bv_decide`. `[[lean::proof("…")]]` to override it is 🟡 specified (main-spec §9.6) but not built |
| `induction` on `Nat`/`List`/`BinTree` | ❌ G27, G3 — `bv_decide` searches a finite space; there is no induction, and a function with an unbounded recursion on `unsigned` has no `VERIFY_FORALL` form |
| any other theorem | 🟡 raw Lean through `LEAN_DECL` (tactics included) |

| Section | Status |
| --- | --- |
| Recursion and Induction (`plusR_zero_left`) | ❌ G27; the finite analogue ✅ |
| The Induction Tactic and its variants (`induction … with`, `case`, `<;>`) | ❌ G27 |
| Induction on Structures (`BinTree.mirror_count`) | ❌ G3, G27 |
| `simp`, `rw`, `grind` | ❌ G27 |
| Exercises (`append_nil`, `append_assoc`, `length_append`, `reverse`) | ❌ G3, G12, G27 |

---

## The finite analogue — what `VERIFY_FORALL` can prove

The book proves facts that hold for *every* natural number. A C author's natural
domain is a machine word, where "for every value" is a finite statement and a SAT
solver decides it — no induction needed. These three are the C analogues of
`plusR_zero_left` (a left identity of `+`), associativity of `++`
(`List.append_assoc`) and an involution:

```c
plusZeroLeft: VERIFY_FORALL(uint8_t k) { 0 + k == k; }

plusAssoc: VERIFY_FORALL(uint8_t a, uint8_t b, uint8_t c) { (a + b) + c == a + (b + c); }

xorTwice: VERIFY_FORALL(bv(8) a, bv(8) b) { ((a ^ b) ^ b) == a; }
```

(The extra parentheses in the last one matter: in C, `==` binds tighter than `^`,
so `(a ^ b) ^ b == a` would mean `(a ^ b) ^ (b == a)`, which Clean rejects as a
comparison used as a number.)

A claim about a function of the file is proved by unfolding the function first
(`simp only [f]`), exactly the step the book's `unfold Nat.plusR` / `simp [Nat.plusR]`
performs by hand:

```c
uint8_t swapNibbles(uint8_t x) { return (x << 4) | (x >> 4); }

swapNibblesTwice: VERIFY_FORALL(uint8_t x) { swapNibbles(swapNibbles(x)) == x; }
```

The theorem is stated as `∀ (x : UInt8), swapNibbles (swapNibbles x) = x` and closed by
`bv_decide`; if it were false Lean would report a counterexample (the translation
still succeeds — a false claim is not hidden, see
`test/cases-lean-rejects/verify_false.c`).

---

## ❌ Proofs by induction (G27, G3)

The recursion on the *second* argument that makes `Nat.plusR` awkward in the previous
chapter is repaired here by induction on `k`. `plusR` itself is a C function:

```c
unsigned plusR(unsigned n, unsigned k) {
  if (k == 0) return n;
  return plusR(n, k - 1) + 1;
}
```

but `∀ k : Nat, k = plusR 0 k` ranges over an unbounded `Nat`, which `VERIFY_FORALL`
refuses (`bv_decide` searches bits; **unsigned/int binders are an error**). The proof
in the book, with each stage of the tactic script:

```lean
theorem plusR_zero_left (k : Nat) : k = Nat.plusR 0 k := by
  induction k

theorem plusR_zero_left (k : Nat) : k = Nat.plusR 0 k := by
  induction k with
  | zero => skip
  | succ n ih => skip

theorem plusR_zero_left (k : Nat) : k = Nat.plusR 0 k := by
  induction k with
  | zero => rfl
  | succ n ih =>
    unfold Nat.plusR
    rw [←ih]

theorem plusR_zero_left (k : Nat) : k = Nat.plusR 0 k := by
  induction k with
  | zero => rfl
  | succ n ih =>
    simp

theorem plusR_zero_left (k : Nat) : k = Nat.plusR 0 k := by
  induction k with
  | zero => rfl
  | succ n ih =>
    simp [Nat.plusR]
    assumption

theorem plusR_zero_left (k : Nat) : k = Nat.plusR 0 k := by
  induction k
  case zero => rfl
  case succ n ih =>
    simp [Nat.plusR]
    assumption

theorem plusR_zero_left (k : Nat) : k = Nat.plusR 0 k := by
  induction k <;> simp [Nat.plusR]

theorem plusR_zero_left (k : Nat) : k = Nat.plusR 0 k := by
  induction k <;> simp [Nat.plusR] <;> assumption
```

The theorem can still be carried through Clean as raw Lean — the translator
neither reads nor checks it, Lean is the only judge (🟡):

```c
unsigned plusR(unsigned n, unsigned k) {
  if (k == 0) return n;
  return plusR(n, k - 1) + 1;
}

LEAN_DECL("theorem plusR_zero_left (k : Nat) : k = plusR 0 k := by induction k <;> simp [plusR] <;> assumption");
```

(Written after the function, `LEAN_DECL` text lands at the end of the file, where it
may mention the functions — main-spec "Raw Lean".)

---

## ❌ Induction on a structure — `BinTree.mirror_count`

Needs the recursive `BinTree` (G3) and its `mirror` from chapter 8 of the book:

```lean
def BinTree.count : BinTree α → Nat
  | .leaf => 0
  | .branch l _ r =>
    1 + l.count + r.count

theorem BinTree.mirror_count (t : BinTree α) :
    t.mirror.count = t.count := by
  induction t with
  | leaf => skip
  | branch l x r ihl ihr => skip

theorem BinTree.mirror_count (t : BinTree α) :
    t.mirror.count = t.count := by
  induction t with
  | leaf => simp [BinTree.mirror]
  | branch l x r ihl ihr =>
    simp [BinTree.mirror, BinTree.count]
    rw [ihl, ihr]
    simp +arith

theorem BinTree.mirror_count (t : BinTree α) :
    t.mirror.count = t.count := by
  induction t with
  | leaf => simp [BinTree.mirror]
  | branch l x r ihl ihr =>
    simp +arith [BinTree.mirror, BinTree.count, *]

theorem BinTree.mirror_count (t : BinTree α) :
    t.mirror.count = t.count := by
  induction t <;> simp +arith [BinTree.mirror, BinTree.count, *]

theorem BinTree.mirror_count (t : BinTree α) :
    t.mirror.count = t.count := by
  induction t <;> grind [BinTree.mirror, BinTree.count]
```

## ❌ Exercises

The exercises prove facts about `List` (`append_nil`, `append_assoc`, `length_append`,
`reverse`-related lemmas). All need `List` (G12) and induction (G27):

```lean
theorem List.append_assoc (xs ys zs : List α) :
    xs ++ (ys ++ zs) = (xs ++ ys) ++ zs
```
