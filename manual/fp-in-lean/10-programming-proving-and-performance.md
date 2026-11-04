# 8. Programming, Proving, and Performance — in Clean

Book chapter: <https://lean-lang.org/functional_programming_in_lean/Programming___-Proving___-and-Performance/>
Legend and gap IDs: see [README.md](README.md).

This chapter is the **closest to C** after chapters 1–2: tail recursion, arrays,
loops, sorting, and the question "when does an update happen in place?". Its other
half — proofs of equivalence and termination — is the part Clean replaces with
`VERIFY_FORALL` over fixed-width values (chapter 3 of this manual) or leaves to raw
Lean.

| Section | Status |
| --- | --- |
| 8.1 Tail Recursion | ✅ the functions on arrays and numbers; `List`/`BinTree` versions ❌ G3, G12 |
| 8.2 Proving Equivalence | 🟡 equivalence of two *finite-width* implementations ✅ `VERIFY_FORALL`; induction on `List` ❌ G27 |
| 8.3 Arrays and Termination | ✅ index loops; termination proofs (`termination_by`, `Nat.lt`, `Prop`s as datatypes) ❌ G18, G27; `Option` result ❌ G1 |
| 8.4 More Inequalities (`mergeSort`) | 🟡 an array merge sort ✅; the `List` version and its termination proof ❌ G3, G18 |
| 8.5 Bounded Numbers (`Fin`) | 🟡 `bv(N)` is arithmetic modulo 2ᴺ like `Fin`; the proof-carrying `Fin n` ❌ G18 |
| 8.6 Insertion Sort and Array Mutation | ✅ the sort and the in-place update; `Ord α` ❌ G4/G20; `dbgTraceIfShared` ❌ |
| 8.7 Special Types | n/a (runtime representation) |
| 8.8 Summary | — |

---

## 8.1 Tail Recursion

A function is tail-recursive when the recursive call is its last action; Lean
compiles it to a loop (so it cannot overflow the stack), C compilers may. The book
contrasts `NonTail.sum` / `Tail.sum`, `NonTail.reverse` / `Tail.reverse`, and several
exercises (`length`, `factorial`, `filter`) on `List`. On **arrays** (the C data
structure) and **numbers**:

```c
int nonTailSum(const int xs[], unsigned n, unsigned i) {   /* NonTail.sum from index i */
  if (i >= n) return 0;
  return xs[i] + nonTailSum(xs, n, i + 1);                 /* addition still to do: not a tail call */
}

int sumHelper(const int xs[], unsigned n, unsigned i, int soFar) {   /* Tail.sumHelper */
  if (i >= n) return soFar;
  return sumHelper(xs, n, i + 1, xs[i] + soFar);                     /* the call is the result */
}

int tailSum(const int xs[], unsigned n) { return sumHelper(xs, n, 0, 0); }   /* Tail.sum */

unsigned nonTailFactorial(unsigned n) {                    /* exercise: NonTail.factorial */
  if (n == 0) return 1;
  return nonTailFactorial(n - 1) * n;
}

unsigned factHelper(unsigned n, unsigned acc) {            /* its tail-recursive form */
  if (n == 0) return acc;
  return factHelper(n - 1, acc * n);
}

unsigned tailFactorial(unsigned n) { return factHelper(n, 1); }

unsigned nonTailLength(unsigned n) {                       /* NonTail.length of a list of n cells */
  if (n == 0) return 0;
  return nonTailLength(n - 1) + 1;
}
```

`reverse` on an array is not a recursion at all: the C idiom swaps from both ends
(a D20 array writer, so `reverse(a, n)` becomes `a := reverse a n`):

```c
void reverse(int a[], unsigned n) {
  for (unsigned i = 0; i < n / 2; i++) {
    int t = a[i];
    a[i] = a[n - 1 - i];
    a[n - 1 - i] = t;
  }
}
```

Clean prints each recursive call exactly as written; Lean compiles tail calls to
jumps. If Lean cannot prove that a recursion terminates (see 8.3) the C function
needs `[[lean::partial]]`.

### ❌ The `List` and `BinTree` versions (G3, G12)

```lean
def NonTail.sum : List Nat → Nat
  | [] => 0
  | x :: xs => x + sum xs

def Tail.sumHelper (soFar : Nat) : List Nat → Nat
  | [] => soFar
  | x :: xs => sumHelper (x + soFar) xs

def Tail.sum (xs : List Nat) : Nat :=
  Tail.sumHelper 0 xs

def NonTail.reverse : List α → List α
  | [] => []
  | x :: xs => reverse xs ++ [x]

def Tail.reverseHelper (soFar : List α) : List α → List α
  | [] => soFar
  | x :: xs => reverseHelper (x :: soFar) xs

def Tail.reverse (xs : List α) : List α :=
  Tail.reverseHelper [] xs

def BinTree.mirror : BinTree α → BinTree α
  | .leaf => .leaf
  | .branch l x r => .branch (mirror r) x (mirror l)

def NonTail.length : List α → Nat
  | [] => 0
  | _ :: xs => NonTail.length xs + 1

def NonTail.factorial : Nat → Nat
  | 0 => 1
  | n + 1 => factorial n * (n + 1)

def NonTail.filter (p : α → Bool) : List α → List α
  | [] => []
  | x :: xs =>
    if p x then
      x :: filter p xs
    else
      filter p xs
```

---

## 8.2 Proving Equivalence

The book proves `NonTail.sum = Tail.sum` by induction, discovering that the helper
needs a *generalised* lemma (`non_tail_sum_eq_helper_accum`, "the accumulator
version") because the induction hypothesis is too weak otherwise. That theorem
quantifies over unbounded lists (G12, G27), so it stays Lean.

The *idea* — "two implementations of the same specification are equal" — is
Clean's headline feature (main-spec §9) **for finite-width values**: state the
equivalence in C and let `bv_decide` check every input.

```c
uint8_t maxA(uint8_t a, uint8_t b) { return a > b ? a : b; }

uint8_t maxB(uint8_t a, uint8_t b) {
  if (b > a) return b;
  return a;
}

maxEq: VERIFY_FORALL(uint8_t a, uint8_t b) { maxA(a, b) == maxB(a, b); }
```

generates `theorem maxEq : ∀ (a b : UInt8), maxA a b = maxB a b` with the functions
unfolded and `bv_decide` as the last step. A recursion on an unbounded `unsigned`
(like `nonTailFactorial`) cannot be a `VERIFY_FORALL` binder.

### ❌ The induction proofs (G27)

```lean
theorem non_tail_sum_eq_tail_sum : NonTail.sum = Tail.sum := by
  funext xs
  induction xs with
  | nil => rfl
  | cons y ys ih =>
    skip

theorem helper_add_sum_accum (xs : List Nat) (n : Nat) :
    n + Tail.sum xs = Tail.sumHelper n xs := by
  skip

theorem non_tail_sum_eq_helper_accum (xs : List Nat) (n : Nat) :
    n + NonTail.sum xs = Tail.sumHelper n xs := by
  fun_induction Tail.sumHelper <;> grind [NonTail.sum]

theorem non_tail_reverse_eq_tail_reverse :
    @NonTail.reverse = @Tail.reverse := by
  funext α xs
  skip
```

(Only the statements and the shape of the final script are reproduced; the book
develops each proof in a dozen intermediate states.)

---

## 8.3 Arrays and Termination

The book maps a function over an array by an index helper, and explains why Lean
needs `inBounds : i < arr.size` (for `arr[i]`) and `termination_by arr.size - i`
(because `i` *grows*). In C the loop is just a loop; Lean's proof obligations
disappear because `for`/`while` are emitted as loops that Lean accepts without a
termination argument, and `arr[i]` is `arr[i]!` (a panic, lint W010, not a proof).

`Array.map` with a *function argument* needs function-typed parameters (G5), so
here the function is fixed. `Array.find` returns an `Option (Nat × α)` (G1, G2),
written as a struct:

```c
typedef struct { bool found; unsigned index; int val; } FindResult;

FindResult find(const int arr[], unsigned n, int target) {   /* Array.find (· == target) */
  FindResult r = {false, 0, 0};
  for (unsigned i = 0; i < n; i++) {
    if (arr[i] == target) {
      r.found = true;
      r.index = i;
      r.val = arr[i];
      return r;
    }
  }
  return r;
}

void mapDouble(int out[], const int arr[], unsigned n) {     /* Array.map (· * 2) */
  for (unsigned i = 0; i < n; i++) out[i] = arr[i] * 2;
}
```

A *recursive* C function over an index (the book's `arrayMapHelper` /
`findHelper`) is emitted as a recursive `def`; Lean must then find a decreasing
measure. For `i` increasing towards `n` it cannot, so either use a loop (as above)
or mark the function `[[lean::partial]]`:

```c
[[lean::partial]] int findIndexFrom(const int arr[], unsigned n, unsigned i, int target) {
  if (i >= n) return -1;
  if (arr[i] == target) return i;
  return findIndexFrom(arr, n, i + 1, target);
}
```

### ❌ Proving termination, and propositions as datatypes (G18, G27)

The middle of the section defines propositions as *inductive predicates* (`IsThree`,
`IsFive`, `Nat.le`) to explain what `i < arr.size` really is:

```lean
def Array.map (f : α → β) (arr : Array α) : Array β :=
  arrayMapHelper f arr Array.empty 0

def arrayMapHelper (f : α → β) (arr : Array α)
    (soFar : Array β) (i : Nat) : Array β :=
  if inBounds : i < arr.size then
    arrayMapHelper f arr (soFar.push (f arr[i])) (i + 1)
  else soFar
termination_by arr.size - i

def Array.find (arr : Array α) (p : α → Bool) :
    Option (Nat × α) :=
  findHelper arr p 0

def findHelper (arr : Array α) (p : α → Bool)
    (i : Nat) : Option (Nat × α) :=
  if h : i < arr.size then
    let x := arr[i]
    if p x then
      some (i, x)
    else findHelper arr p (i + 1)
  else none
termination_by arr.size - i

inductive EasyToProve : Prop where
  | heresTheProof : EasyToProve

theorem fairlyEasy : EasyToProve := by
  constructor

inductive IsThree : Nat → Prop where
  | isThree : IsThree 3

theorem three_is_three : IsThree 3 := by
  constructor

inductive IsFive : Nat → Prop where
  | isFive : IsFive 5

theorem three_plus_two_five : IsThree n → IsFive (n + 2) := by
  intro three
  cases three with
  | isThree => skip

theorem four_is_not_three : ¬ IsThree 4 := by
  intro h
  cases h

inductive Nat.le (n : Nat) : Nat → Prop
  | refl : Nat.le n n
  | step : Nat.le n m → Nat.le n (m + 1)

theorem four_le_seven : 4 ≤ 7 :=
  open Nat.le in
  step (step (step refl))

def Nat.lt (n m : Nat) : Prop :=
  Nat.le (n + 1) m

theorem four_lt_seven : 4 < 7 :=
  open Nat.le in
  step (step refl)
```

---

## 8.4 More Inequalities — `mergeSort`

`mergeSort` splits a list in halves (`splitList`), sorts each, and merges. The
book's difficulty is proving the halves are *strictly shorter* so the recursion
terminates. In C, merge sort on an array is iterative (bottom-up): no termination
proof, one scratch buffer. The scratch array is a *local* here (Clean refuses a
second written array parameter, D20), so its size is fixed:

```c
void mergeSort(int a[], unsigned n) {
  int tmp[64] = {0};                                     /* scratch space: n must be ≤ 64 */
  for (unsigned width = 1; width < n; width = width * 2) {
    for (unsigned lo = 0; lo < n; lo = lo + 2 * width) {
      unsigned mid = lo + width;
      if (mid > n) mid = n;
      unsigned hi = lo + 2 * width;
      if (hi > n) hi = n;

      unsigned i = lo;                                   /* merge a[lo,mid) and a[mid,hi) */
      unsigned j = mid;
      for (unsigned k = lo; k < hi; k++) {
        if (i < mid && (j >= hi || a[i] <= a[j])) {
          tmp[k] = a[i];
          i++;
        } else {
          tmp[k] = a[j];
          j++;
        }
      }
    }
    for (unsigned k = 0; k < n; k++) a[k] = tmp[k];
  }
}
```

(`a[i] <= a[j]` takes the left element on ties, like the book's `.lt | .eq =>`
case, so the sort is stable; the `Ord α` class becomes `int`'s `<=`, G4/G20.)

### ❌ The `List` version and the termination proof (G3, G18, G27)

```lean
def merge [Ord α] (xs : List α) (ys : List α) : List α :=
  match xs, ys with
  | [], _ => ys
  | _, [] => xs
  | x'::xs', y'::ys' =>
    match Ord.compare x' y' with
    | .lt | .eq => x' :: merge xs' (y' :: ys')
    | .gt => y' :: merge (x'::xs') ys'

def splitList (lst : List α) : (List α × List α) :=
  match lst with
  | [] => ([], [])
  | x :: xs =>
    let (a, b) := splitList xs
    (x :: b, a)

def mergeSort [Ord α] (xs : List α) : List α :=
  if h : xs.length < 2 then
    match xs with
    | [] => []
    | [x] => [x]
  else
    let halves := splitList xs
    merge (mergeSort halves.fst) (mergeSort halves.snd)
termination_by xs.length

theorem splitList_shorter_le (lst : List α) :
    (splitList lst).fst.length ≤ lst.length ∧
      (splitList lst).snd.length ≤ lst.length := by
  skip

theorem Nat.le_succ_of_le (h : n ≤ m) : n ≤ m + 1:= by
  grind

theorem splitList_shorter (lst : List α) (_ : lst.length ≥ 2) :
    (splitList lst).fst.length < lst.length ∧
      (splitList lst).snd.length < lst.length := by
  skip
```

---

## 8.5 Bounded Numbers — `Fin`

`Fin n` is a number paired with a proof that it is below `n`; `arr[i]` with
`i : Fin arr.size` needs no further check. Arithmetic on `Fin n` wraps modulo `n`,
which is exactly what C programmers get from a fixed-width integer; Clean's
`bv(N)` is modulo 2ᴺ (🟡 only for power-of-two `n`):

```c
bv(3) next(bv(3) x) { return x + BV(3, 1); }     /* (7 : Fin 8) + 1 wraps to 0 */

bv(3) seven(void) { return BV(3, 7); }
```

An index of a C array is an `unsigned` (a `Nat`); there is no type that says "valid
index of *this* array" (G18).

```lean
structure Fin (n : Nat) where
  val  : Nat
  isLt : LT.lt val n

def findHelper (arr : Array α) (p : α → Bool) (i : Nat) :
    Option (Fin arr.size × α) :=
  if h : i < arr.size then
    let x := arr[i]
    if p x then
      some (⟨i, h⟩, x)
    else findHelper arr p (i + 1)
  else none

def Array.find (arr : Array α) (p : α → Bool) : Option (Fin arr.size × α) :=
  findHelper arr p 0

#eval (3 : Fin 8).next?
#eval (7 : Fin 8).next?
```

---

## 8.6 Insertion Sort and Array Mutation

The book's sort: `insertSorted` moves the element at `i` left until it is no longer
smaller than its neighbour, `insertionSortLoop` calls it for every `i`. `Array.swap`
is two element writes. In C it is natural either as the book's recursion or as a
`while` loop; both are D20 array writers, so each call `insertSorted(arr, i);`
becomes `arr := insertSorted arr i`:

```c
void insertSorted(int arr[], unsigned i) {             /* the book's recursion, on a Nat index */
  if (i == 0) return;
  if (arr[i - 1] > arr[i]) {                           /* .gt: swap and continue */
    int t = arr[i - 1];
    arr[i - 1] = arr[i];
    arr[i] = t;
    insertSorted(arr, i - 1);
  }                                                    /* .lt | .eq: done */
}

void insertionSort(int arr[], unsigned n) {
  for (unsigned i = 0; i < n; i++) insertSorted(arr, i);     /* insertionSortLoop arr 0 */
}
```

The loop form, and the same sort on strings (the book sorts lines of text; `Ord α`
becomes `strcmp`, one function per element type, G4/G20):

```c
typedef const char *Str;

void insertSortedStr(Str arr[], unsigned i) {
  while (i > 0 && strcmp(arr[i - 1], arr[i]) > 0) {
    Str t = arr[i - 1];
    arr[i - 1] = arr[i];
    arr[i] = t;
    i--;
  }
}

void insertionSortStr(Str arr[], unsigned n) {
  for (unsigned i = 0; i < n; i++) insertSortedStr(arr, i);
}
```

The recursive `insertSorted` above needs Lean to prove `i - 1 < i` terminates
(automatic: `i` is a `Nat` that decreases). The book's extra work — `Fin arr.size`
indices, a proof that `insertSorted` preserves the array's size
(`insert_sorted_size_eq`, so that `insertionSortLoop`'s measure `arr.size - i`
decreases) — is the termination argument of G18/G27. With a C `for` loop none of it
is needed.

### Performance: in-place updates and sharing

Lean arrays are values, but when a value has one owner the update `arr.set! i v`
happens **in place** (reference counting); when two names share it, the first write
copies. The book shows this with `getLines`, `mainUnique` (sorted array is the only
reference: in place) and `mainShared` (the original is printed afterwards: one
copy). Clean's arrays are the same `Array`, so a D20 writer is the in-place case:
`arr := insertionSortStr arr n`. The *shared* case is an explicit copy in C, which
makes the cost visible (and is how a C programmer keeps the original):

```c
typedef const char *Str;

void insertionSortStr(Str arr[], unsigned n) {
  for (unsigned i = 0; i < n; i++) {
    unsigned j = i;
    while (j > 0 && strcmp(arr[j - 1], arr[j]) > 0) {
      Str t = arr[j - 1];
      arr[j - 1] = arr[j];
      arr[j] = t;
      j--;
    }
  }
}

int main(int argc, char *argv[]) {
  Str lines[64] = {""};
  Str sorted[64] = {""};
  unsigned count = 0;
  char cur[256] = "";

  fgets(cur, 256, stdin);                              /* getLines */
  while (strcmp(cur, "") != 0) {
    lines[count] = cur;
    count++;
    cur = "";
    fgets(cur, 256, stdin);
  }

  for (unsigned i = 0; i < count; i++) sorted[i] = lines[i];   /* the copy `mainShared` pays for */
  insertionSortStr(sorted, count);

  if (argc == 2 && strcmp(argv[1], "--shared") == 0) {
    puts("--- Sorted lines: ---");
    for (unsigned i = 0; i < count; i++) printf("%s", sorted[i]);
    puts("");
    puts("--- Original data: ---");
    for (unsigned i = 0; i < count; i++) printf("%s", lines[i]);
    return 0;
  }
  if (argc == 2 && strcmp(argv[1], "--unique") == 0) {
    for (unsigned i = 0; i < count; i++) printf("%s", sorted[i]);
    return 0;
  }
  puts("Expected either \"--shared\" or \"--unique\"");
  return 1;
}
```

(Two differences from the book: `fgets` keeps the trailing newline, so lines are
printed with `%s` and no `println`, and the unique case still copies, because a
C program has no way to say "I am done with `lines`". `lines.push` becomes a store
into a fixed-size array, G12.)

### ❌ `Ord α`, `Fin` arrays, `dbgTraceIfShared`, the proofs (G4, G18, G20, G27)

```lean
#check dbgTraceIfShared

def insertSorted [Ord α] (arr : Array α) (i : Fin arr.size) : Array α :=
  match i with
  | ⟨0, _⟩ => arr
  | ⟨i' + 1, _⟩ =>
    match Ord.compare arr[i'] arr[i] with
    | .lt | .eq => arr
    | .gt =>
      insertSorted (arr.swap i' i) ⟨i', by grind⟩

def insertionSortLoop [Ord α] (arr : Array α) (i : Nat) : Array α :=
  if h : i < arr.size then
    insertionSortLoop (insertSorted arr ⟨i, h⟩) (i + 1)
  else
    arr

partial def insertionSortLoop [Ord α] (arr : Array α) (i : Nat) : Array α :=
  if h : i < arr.size then
    insertionSortLoop (insertSorted arr ⟨i, h⟩) (i + 1)
  else
    arr

#eval insertionSortLoop #[5, 17, 3, 8] 0
#eval insertionSortLoop #["metamorphic", "igneous", "sedimentary"] 0

theorem insert_sorted_size_eq [Ord α]
    (arr : Array α) (i : Fin arr.size) :
    (insertSorted arr i).size = arr.size := by
  fun_induction insertSorted <;> grind

def insertionSortLoop [Ord α] (arr : Array α) (i : Nat) : Array α :=
  if h : i < arr.size then
    have : (insertSorted arr ⟨i, h⟩).size - (i + 1) < arr.size - i := by
      grind [insert_sorted_size_eq]
    insertionSortLoop (insertSorted arr ⟨i, h⟩) (i + 1)
  else
    arr
termination_by arr.size - i

def insertionSort [Ord α] (arr : Array α) : Array α :=
  insertionSortLoop arr 0

#eval insertionSort #[3, 1, 7, 4]
#eval insertionSort #[ "quartz", "marble", "granite", "hematite"]

def insertSorted [Ord α] (arr : Array α) (i : Fin arr.size) : Array α :=
  match i with
  | ⟨0, _⟩ => arr
  | ⟨i' + 1, _⟩ =>
    have : i' < arr.size := by
      grind
    match Ord.compare arr[i'] arr[i] with
    | .lt | .eq => arr
    | .gt =>
      have : (dbgTraceIfShared "array to swap" arr).size = arr.size := by
        grind [dbgTraceIfShared]
      insertSorted
        ((dbgTraceIfShared "array to swap" arr).swap i' i)
        ⟨i', by grind [dbgTraceIfShared]⟩

def getLines : IO (Array String) := do
  let stdin ← IO.getStdin
  let mut lines : Array String := #[]
  let mut currLine ← stdin.getLine
  while !currLine.isEmpty do
     -- Drop trailing newline:
    lines := lines.push (currLine.dropEnd 1).copy
    currLine ← stdin.getLine
  pure lines

def mainUnique : IO Unit := do
  let lines ← getLines
  for line in insertionSort lines do
    IO.println line

def mainShared : IO Unit := do
  let lines ← getLines
  IO.println "--- Sorted lines: ---"
  for line in insertionSort lines do
    IO.println line

  IO.println ""
  IO.println "--- Original data: ---"
  for line in lines do
    IO.println line

def main (args : List String) : IO UInt32 := do
  match args with
  | ["--shared"] => mainShared; pure 0
  | ["--unique"] => mainUnique; pure 0
  | _ =>
    IO.println "Expected either \"--shared\" or \"--unique\""
    pure 1
```

The book runs the program with `cat` on a text file and reads the debug trace of
`dbgTraceIfShared` on standard error: "shared RC array to swap" appears once in
the `--shared` run and never in the `--unique` run. Clean has no `dbgTraceIfShared`
(G28); if you need it, use raw Lean (`LEAN_DECL` around the whole function).

---

## 8.7 Special Types — n/a

Runtime representations: a `structure` with one field is stored as that field,
`Nat` small values are unboxed, `UInt8`…`UInt64` are machine words, `Float` is an
IEEE double, `Array` is a growable buffer. The C author does not choose any of this;
Clean maps `uint8_t…uint64_t` to Lean's like-named types (so they are machine words
and wrap), `unsigned`/`int` to the unbounded `Nat`/`Int`, and `double` to `Float`.
Performance consequences of picking `int` where C would use a 32-bit word are the
subject of lint W001 (`--lint=c-semantics`).

---

## 8.8 Summary

The takeaways — prefer tail recursion for accumulating, loops for arrays, prove
equivalences and termination when a proof is the goal, and know when an update is
in place — map to the C of this chapter: loops for arrays, `VERIFY_FORALL` for
equivalences of finite-width functions, `[[lean::partial]]` when termination is
not provable, and D20 writers for in-place array updates.
