# 4. Monads — in Clean

Book chapter: <https://lean-lang.org/functional_programming_in_lean/Monads/>
Legend and gap IDs: see [README.md](README.md).
C/C++-style syntax for what this chapter misses is designed (not built) in
[spec/cpp-front-end-design.md](../../spec/cpp-front-end-design.md): §5 (tagged unions, `std::optional` → `Option`, `std::expected` → `Except`, the book's `evaluate`) and §10 (`co_await` as `←`).

The chapter's thesis is that "stop at the first failure", "collect a log", and
"thread a counter" are *patterns of sequencing* that the `Monad` type class
captures once. C already has all three patterns, in their *non-abstracted*
form: early `return`, an extra output field, an explicit state parameter. Clean
accepts exactly those, and emits Lean's `do` notation for them (§4.4 below shows
it). What Clean cannot express is the abstraction itself — functions generic in
the monad — because that needs higher-kinded polymorphism and type classes
(**G21**, **G20**).

| Section | Status |
| --- | --- |
| 4.1 One API, Many Applications | 🟡 the three patterns as C idioms ✅; `Option`/`Except`/`State` types and `andThen`/`~~>` ❌ G1, G5, G21 |
| 4.2 The Monad Type Class | ❌ G20, G21 |
| 4.3 Example: Arithmetic in Monads | 🟡 `applyPrim`/`applyDiv` ✅; `Expr` tree and `evaluateM` ❌ G3, G21 |
| 4.4 `do`-Notation for Monads | ✅ it is what Clean emits for every function with statements |
| 4.5 The `IO` Monad | ✅ every `printf`/`scanf` function is `IO` (see chapter 2) |
| 4.6 Additional Conveniences | 🟡 `switch` or-patterns ✅; pipelines, `fun` ❌ G5 |

---

## 4.1 One API, Many Applications

### Failure: `Option` as "maybe missing"

The book's `firstThird` looks up positions 0 and 2 and gives `none` when either
is missing, then rewrites the nested `match` with `andThen`. In C the same
program is two guarded reads with early exit. There is no `Option`, so a
two-field struct stands in (G1); `at` is a reserved Lean word, so the helper is
`lookup` (D8):

```c
typedef struct { bool ok; int val; } OptionInt;
typedef struct { bool ok; int first; int third; } OptionPair;

OptionInt lookup(const int xs[], unsigned n, unsigned i) {   /* xs[i]? */
  OptionInt r = {false, 0};
  if (i < n) {
    r.ok = true;
    r.val = xs[i];
  }
  return r;
}

OptionPair firstThird(const int xs[], unsigned n) {
  OptionPair none = {false, 0, 0};

  OptionInt first = lookup(xs, n, 0);
  if (!first.ok) return none;
  OptionInt third = lookup(xs, n, 2);
  if (!third.ok) return none;

  OptionPair r = {true, first.val, third.val};
  return r;
}
```

The staircase of nested `match`es in `firstThirdFifthSeventh` is the same
function with four guarded reads; early `return` *is* what `andThen` captures
(see 4.4 for the Lean that Clean generates).

### Failure with a message: `Except`

`get` reports *which* index was missing; `sprintf` builds the message
(`Index 4 not found (maximum is 3)`):

```c
typedef struct { bool ok; const char *err; int val; } ExceptInt;

ExceptInt get(const int xs[], unsigned n, unsigned i) {
  ExceptInt r = {true, "", 0};
  if (i >= n) {
    char msg[100] = "";
    sprintf(msg, "Index %d not found (maximum is %d)", i, n - 1);
    r.ok = false;
    r.err = msg;
    return r;
  }
  r.val = xs[i];
  return r;
}
```

### Logging: `WithLog`

`sumAndFindEvens` returns both the evens seen and the sum. The book threads the
log through `andThen`; C accumulates it in a struct (the log has a fixed
capacity here, the price of no `List`, G12):

```c
typedef struct { int log[16]; unsigned count; int val; } WithLog;

bool isEven(int i) { return i % 2 == 0; }

WithLog sumAndFindEvens(const int xs[], unsigned n) {
  WithLog r = {{0}, 0, 0};
  for (unsigned k = 0; k < n; k++) {
    if (isEven(xs[k])) {              /* save i */
      r.log[r.count] = xs[k];
      r.count++;
    }
    r.val += xs[k];
  }
  return r;
}
```

### State: threading a counter

`State σ α = σ → (σ × α)` is a function value (G5). Its C form passes the state
in and returns it, which is the book's `increment` before the monad is
introduced; `mapM increment [1, 2, 3, 4, 5]` starting from 0 gives
`(15, [0, 1, 3, 6, 10])`:

```c
typedef struct { int state; int val; } IntState;

IntState increment(int howMuch, int s) {
  IntState r = {s + howMuch, s};      /* the new state, and the old value */
  return r;
}

typedef struct { int state; int out[5]; } MapResult;

MapResult mapIncrement(const int xs[], unsigned n, int s) {
  MapResult r = {s, {0}};
  for (unsigned i = 0; i < n; i++) {
    r.out[i] = r.state;
    r.state = r.state + xs[i];
  }
  return r;
}

const int steps[5] = {1, 2, 3, 4, 5};
const MapResult result = mapIncrement(steps, 5, 0);    /* state 15, out [0, 1, 3, 6, 10] */
```

### ❌ `number` — state over a *tree* (G3, G5, G21)

Numbering the nodes of a `BinTree` threads the state through recursive calls
on a recursive type. Clean has no recursive types (`BinTree` needs pointers or
tagged unions), no function values for `State`, and no `~~>` combinators:

```lean
inductive BinTree (α : Type) where
  | leaf : BinTree α
  | branch : BinTree α → α → BinTree α → BinTree α

def number (t : BinTree α) : BinTree (Nat × α) :=
  let rec helper (n : Nat) : BinTree α → (Nat × BinTree (Nat × α))
    | BinTree.leaf => (n, BinTree.leaf)
    | BinTree.branch left x right =>
      let (k, numberedLeft) := helper n left
      let (i, numberedRight) := helper (k + 1) right
      (i, BinTree.branch numberedLeft (k, x) numberedRight)
  (helper 0 t).snd

def State (σ : Type) (α : Type) : Type :=
  σ → (σ × α)

def ok (x : α) : State σ α :=
  fun s => (s, x)

def get : State σ σ :=
  fun s => (s, s)

def set (s : σ) : State σ Unit :=
  fun _ => (s, ())

def andThen (first : State σ α) (next : α → State σ β) : State σ β :=
  fun s =>
    let (s', x) := first s
    next x s'

infixl:55 " ~~> " => andThen

def number (t : BinTree α) : BinTree (Nat × α) :=
  let rec helper : BinTree α → State Nat (BinTree (Nat × α))
    | BinTree.leaf => ok BinTree.leaf
    | BinTree.branch left x right =>
      helper left ~~> fun numberedLeft =>
      get ~~> fun n =>
      set (n + 1) ~~> fun () =>
      helper right ~~> fun numberedRight =>
      ok (BinTree.branch numberedLeft (n, x) numberedRight)
  (helper t 0).snd
```

### ❌ The chaining operators and the generic types (G1, G2, G5)

The *point* of the section — that `andThen` has the same shape for `Option`,
`Except`, `WithLog` and `State` — is the part with no C counterpart:

```lean
def first (xs : List α) : Option α :=
  xs[0]?

def firstThird (xs : List α) : Option (α × α) :=
  match xs[0]? with
  | none => none
  | some first =>
    match xs[2]? with
    | none => none
    | some third =>
      some (first, third)

def andThen (opt : Option α) (next : α → Option β) : Option β :=
  match opt with
  | none => none
  | some x => next x

def firstThird (xs : List α) : Option (α × α) :=
  andThen xs[0]? fun first =>
  andThen xs[2]? fun third =>
  some (first, third)

infixl:55 " ~~> " => andThen

def firstThirdInfix (xs : List α) : Option (α × α) :=
  xs[0]? ~~> fun first =>
  xs[2]? ~~> fun third =>
  some (first, third)

inductive Except (ε : Type) (α : Type) where
  | error : ε → Except ε α
  | ok : α → Except ε α
deriving BEq, Hashable, Repr

def get (xs : List α) (i : Nat) : Except String α :=
  match xs[i]? with
  | none => Except.error s!"Index {i} not found (maximum is {xs.length - 1})"
  | some x => Except.ok x

def andThen (attempt : Except e α) (next : α → Except e β) : Except e β :=
  match attempt with
  | Except.error msg => Except.error msg
  | Except.ok x => next x

def ok (x : α) : Except ε α := Except.ok x
def fail (err : ε) : Except ε α := Except.error err

structure WithLog (logged : Type) (α : Type) where
  log : List logged
  val : α

def andThen (result : WithLog α β) (next : β → WithLog α γ) : WithLog α γ :=
  let {log := thisOut, val := thisRes} := result
  let {log := nextOut, val := nextRes} := next thisRes
  {log := thisOut ++ nextOut, val := nextRes}

def ok (x : β) : WithLog α β := {log := [], val := x}

def save (data : α) : WithLog α Unit :=
  {log := [data], val := ()}

def sumAndFindEvens : List Int → WithLog Int Int
  | [] => ok 0
  | i :: is =>
    andThen (if isEven i then save i else ok ()) fun () =>
    andThen (sumAndFindEvens is) fun sum =>
    ok (i + sum)

def sumAndFindEvens : List Int → List Int × Int
  | [] => ([], 0)
  | i :: is =>
    let (moreEven, sum) := sumAndFindEvens is
    (if isEven i then i :: moreEven else moreEven, sum + i)
```

---

## 4.2 The Monad Type Class — ❌ (G20, G21)

`class Monad (m : Type → Type)` abstracts over a type *constructor*; `instance :
Monad Option`, `Monad (Except ε)`, `Monad (State σ)`, `Monad (WithLog logged)`,
`Id`, and the generic `mapM`, `firstThirdFifthSeventh [Monad m]` all depend on it.
Clean has neither higher-kinded types nor classes:

```lean
class Monad (m : Type → Type) where
  pure : α → m α
  bind : m α → (α → m β) → m β

instance : Monad Option where
  pure x := some x
  bind opt next :=
    match opt with
    | none => none
    | some x => next x

instance : Monad (Except ε) where
  pure x := Except.ok x
  bind attempt next :=
    match attempt with
    | Except.error e => Except.error e
    | Except.ok x => next x

def firstThirdFifthSeventh [Monad m] (lookup : List α → Nat → m α)
    (xs : List α) : m (α × α × α × α) :=
  lookup xs 0 >>= fun first =>
  lookup xs 2 >>= fun third =>
  lookup xs 4 >>= fun fifth =>
  lookup xs 6 >>= fun seventh =>
  pure (first, third, fifth, seventh)

def mapM [Monad m] (f : α → m β) : List α → m (List β)
  | [] => pure []
  | x :: xs =>
    f x >>= fun hd =>
    mapM f xs >>= fun tl =>
    pure (hd :: tl)

instance : Monad (State σ) where
  pure x := fun s => (s, x)
  bind first next :=
    fun s =>
      let (s', x) := first s
      next x s'

def increment (howMuch : Int) : State Int Int :=
  get >>= fun i =>
  set (i + howMuch) >>= fun () =>
  pure i

instance : Monad (WithLog logged) where
  pure x := {log := [], val := x}
  bind result next :=
    let {log := thisOut, val := thisRes} := result
    let {log := nextOut, val := nextRes} := next thisRes
    {log := thisOut ++ nextOut, val := nextRes}

def saveIfEven (i : Int) : WithLog Int Int :=
  (if isEven i then
    save i
   else pure ()) >>= fun () =>
  pure i

def Id (t : Type) : Type := t

instance : Monad Id where
  pure x := x
  bind x f := f x

def numbers := mapM (m := Id) (do return · + 1) [1, 2, 3, 4, 5]
```

---

## 4.3 Example: Arithmetic in Monads

`Arith` (`plus | minus | times | div`) is an `enum`; the one operation that can
fail is division. `applyPrim` for the `Except String` flavour is a function
returning the same struct as `get` above:

```c
typedef struct { bool ok; const char *err; int val; } ExceptInt;

enum Arith { plus, minus, times, divide };

ExceptInt applyPrim(enum Arith p, int x, int y) {
  ExceptInt r = {true, "", 0};
  switch (p) {
    case plus:  r.val = x + y; break;
    case minus: r.val = x - y; break;
    case times: r.val = x * y; break;
    case divide:
      if (y == 0) {
        char msg[100] = "";
        sprintf(msg, "Tried to divide %d by zero", x);
        r.ok = false;
        r.err = msg;
      } else {
        r.val = x / y;
      }
      break;
  }
  return r;
}
```

(The book's `div` constructor is spelled `divide` here only to avoid the `<stdlib.h>` function `div`; Clean accepts `div` too.)
The `Option` flavour (`applyPrimOption`, `none` on division by zero) is the same
function with `OptionInt`; a version of `evaluate` that works for *both*
(`evaluateM`, taking `applyDiv` as an argument, §4.3.1) needs function-valued
parameters and a generic monad (G5, G21).

### ❌ The expression tree and `evaluateM` (G3, G5, G21)

`Expr op` is a recursive `inductive` with payloads (G3); the evaluators recurse
on it in several monads (`Option`, `Except`, `Many` for nondeterminism, `Reader`
for an environment of operations, `WithLog` for a trace):

```lean
inductive Expr (op : Type) where
  | const : Int → Expr op
  | prim : op → Expr op → Expr op → Expr op

inductive Arith where
  | plus
  | minus
  | times
  | div

open Expr in
open Arith in
def twoPlusThree : Expr Arith :=
  prim plus (const 2) (const 3)

open Expr in
open Arith in
def fourteenDivided : Expr Arith :=
  prim div (const 14)
    (prim minus (const 45)
      (prim times (const 5)
        (const 9)))

def evaluateOption : Expr Arith → Option Int
  | Expr.const i => pure i
  | Expr.prim p e1 e2 =>
    evaluateOption e1 >>= fun v1 =>
    evaluateOption e2 >>= fun v2 =>
    match p with
    | Arith.plus => pure (v1 + v2)
    | Arith.minus => pure (v1 - v2)
    | Arith.times => pure (v1 * v2)
    | Arith.div => if v2 == 0 then none else pure (v1 / v2)

def applyPrim : Arith → Int → Int → Except String Int
  | Arith.plus, x, y => pure (x + y)
  | Arith.minus, x, y => pure (x - y)
  | Arith.times, x, y => pure (x * y)
  | Arith.div, x, y =>
    if y == 0 then
      Except.error s!"Tried to divide {x} by zero"
    else pure (x / y)

def evaluateExcept : Expr Arith → Except String Int
  | Expr.const i => pure i
  | Expr.prim p e1 e2 =>
    evaluateExcept e1 >>= fun v1 =>
    evaluateExcept e2 >>= fun v2 =>
    applyPrim p v1 v2

def evaluateM [Monad m]
    (applyPrim : Arith → Int → Int → m Int) :
    Expr Arith → m Int
  | Expr.const i => pure i
  | Expr.prim p e1 e2 =>
    evaluateM applyPrim e1 >>= fun v1 =>
    evaluateM applyPrim e2 >>= fun v2 =>
    applyPrim p v1 v2

def applyDivOption (x : Int) (y : Int) : Option Int :=
    if y == 0 then
      none
    else pure (x / y)

def applyDivExcept (x : Int) (y : Int) : Except String Int :=
    if y == 0 then
      Except.error s!"Tried to divide {x} by zero"
    else pure (x / y)

def applyPrim [Monad m]
    (applyDiv : Int → Int → m Int) :
    Arith → Int → Int → m Int
  | Arith.plus, x, y => pure (x + y)
  | Arith.minus, x, y => pure (x - y)
  | Arith.times, x, y => pure (x * y)
  | Arith.div, x, y => applyDiv x y

def evaluateM [Monad m]
    (applyDiv : Int → Int → m Int) :
    Expr Arith → m Int
  | Expr.const i => pure i
  | Expr.prim p e1 e2 =>
    evaluateM applyDiv e1 >>= fun v1 =>
    evaluateM applyDiv e2 >>= fun v2 =>
    applyPrim applyDiv p v1 v2

inductive Prim (special : Type) where
  | plus
  | minus
  | times
  | other : special → Prim special

inductive CanFail where
  | div

def divOption : CanFail → Int → Int → Option Int
  | CanFail.div, x, y => if y == 0 then none else pure (x / y)

def divExcept : CanFail → Int → Int → Except String Int
  | CanFail.div, x, y =>
    if y == 0 then
      Except.error s!"Tried to divide {x} by zero"
    else pure (x / y)

def applyPrim [Monad m]
    (applySpecial : special → Int → Int → m Int) :
    Prim special → Int → Int → m Int
  | Prim.plus, x, y => pure (x + y)
  | Prim.minus, x, y => pure (x - y)
  | Prim.times, x, y => pure (x * y)
  | Prim.other op, x, y => applySpecial op x y

def evaluateM [Monad m]
    (applySpecial : special → Int → Int → m Int) :
    Expr (Prim special) → m Int
  | Expr.const i => pure i
  | Expr.prim p e1 e2 =>
    evaluateM applySpecial e1 >>= fun v1 =>
    evaluateM applySpecial e2 >>= fun v2 =>
    applyPrim applySpecial p v1 v2

def applyEmpty [Monad m] (op : Empty) (_ : Int) (_ : Int) : m Int :=
  nomatch op
```

### ❌ Nondeterminism (`Many`), environments (`Reader`), traces (`WithLog` + `ToTrace`)

All of these are further monads, each a type constructor with its own
`instance : Monad …` (G20, G21); `Many` additionally holds *thunks*
(`Unit → Many α`, i.e. function values, G5) and `addsTo` searches subset sums
lazily:

```lean
inductive Many (α : Type) where
  | none : Many α
  | more : α → (Unit → Many α) → Many α

def Many.one (x : α) : Many α := Many.more x (fun () => Many.none)

def Many.union : Many α → Many α → Many α
  | Many.none, ys => ys
  | Many.more x xs, ys => Many.more x (fun () => union (xs ()) ys)

def Many.fromList : List α → Many α
  | [] => Many.none
  | x :: xs => Many.more x (fun () => fromList xs)

def Many.take : Nat → Many α → List α
  | 0, _ => []
  | _ + 1, Many.none => []
  | n + 1, Many.more x xs => x :: (xs ()).take n

def Many.takeAll : Many α → List α
  | Many.none => []
  | Many.more x xs => x :: (xs ()).takeAll

def Many.bind : Many α → (α → Many β) → Many β
  | Many.none, _ =>
    Many.none
  | Many.more x xs, f =>
    (f x).union (bind (xs ()) f)

instance : Monad Many where
  pure := Many.one
  bind := Many.bind

def addsTo (goal : Nat) : List Nat → Many (List Nat)
  | [] =>
    if goal == 0 then
      pure []
    else
      Many.none
  | x :: xs =>
    if x > goal then
      addsTo goal xs
    else
      (addsTo goal xs).union
        (addsTo (goal - x) xs >>= fun answer =>
         pure (x :: answer))

inductive NeedsSearch
  | div
  | choose

def applySearch : NeedsSearch → Int → Int → Many Int
  | NeedsSearch.choose, x, y =>
    Many.fromList [x, y]
  | NeedsSearch.div, x, y =>
    if y == 0 then
      Many.none
    else Many.one (x / y)

def Reader (ρ : Type) (α : Type) : Type := ρ → α

def read : Reader ρ ρ := fun env => env

def Reader.pure (x : α) : Reader ρ α := fun _ => x

def Reader.bind
    (result : Reader ρ α)
    (next : α → Reader ρ β) : Reader ρ β :=
  fun env => next (result env) env

instance : Monad (Reader ρ) where
  pure x := fun _ => x
  bind x f := fun env => f (x env) env

abbrev Env : Type := List (String × (Int → Int → Int))

def exampleEnv : Env := [("max", max), ("mod", (· % ·))]

def applyPrimReader (op : String) (x : Int) (y : Int) : Reader Env Int :=
  read >>= fun env =>
  match env.lookup op with
  | none => pure 0
  | some f => pure (f x y)

def ReaderOption (ρ : Type) (α : Type) : Type := ρ → Option α
def ReaderExcept (ε : Type) (ρ : Type) (α : Type) : Type := ρ → Except ε α

inductive ToTrace (α : Type) : Type where
  | trace : α → ToTrace α
```

---

## 4.4 `do`-Notation for Monads

The book rewrites `>>=` chains into `do` blocks:

```lean
def firstThirdFifthSeventh [Monad m] (lookup : List α → Nat → m α)
    (xs : List α) : m (α × α × α × α) := do
  let first ← lookup xs 0
  let third ← lookup xs 2
  let fifth ← lookup xs 4
  let seventh ← lookup xs 6
  pure (first, third, fifth, seventh)
```

For Clean this is not a feature to learn: **every C function that contains
statements is emitted as a `do` block** (main-spec §3.3, D7), and an early
`return` is `do`'s early exit. The C below is the book's function with the
`Option` monad replaced by an explicit `ok` flag:

```c
typedef struct { bool ok; int val; } OptionInt;
typedef struct { bool ok; int a; int b; int c; int d; } Opt4;

OptionInt lookup(const int xs[], unsigned n, unsigned i) {
  OptionInt r = {false, 0};
  if (i < n) {
    r.ok = true;
    r.val = xs[i];
  }
  return r;
}

Opt4 firstThirdFifthSeventh(const int xs[], unsigned n) {
  Opt4 none = {false, 0, 0, 0, 0};

  OptionInt first = lookup(xs, n, 0);
  if (!first.ok) return none;
  OptionInt third = lookup(xs, n, 2);
  if (!third.ok) return none;
  OptionInt fifth = lookup(xs, n, 4);
  if (!fifth.ok) return none;
  OptionInt seventh = lookup(xs, n, 6);
  if (!seventh.ok) return none;

  Opt4 r = {true, first.val, third.val, fifth.val, seventh.val};
  return r;
}
```

and this is the Lean `clean` writes for the second function (copied from the
translator's actual output):

```lean
def firstThirdFifthSeventh (xs : Array Int) (n : Nat) : Opt4 := Id.run do
  let none : Opt4 := ⟨false, 0, 0, 0, 0⟩
  let first : OptionInt := lookup xs n 0
  if ¬(first.ok) then
    return none
  let third : OptionInt := lookup xs n 2
  if ¬(third.ok) then
    return none
  let fifth : OptionInt := lookup xs n 4
  if ¬(fifth.ok) then
    return none
  let seventh : OptionInt := lookup xs n 6
  if ¬(seventh.ok) then
    return none
  let r : Opt4 := ⟨true, first.val, third.val, fifth.val, seventh.val⟩
  return r
```

The nested-action shorthand `pure ((← f x) :: (← mapM f xs))` is C's ordinary
expression nesting, with the restriction explained in chapter 2 (§2.5: no I/O
call inside a branch of `?:`, `&&`, `||`).

---

## 4.5 The `IO` Monad

The section opens the hood on `IO`: `IO` is `EIO IO.Error`, which is `EST ε
IO.RealWorld α`, a function from a world token to either a result or an error.
None of this has a C counterpart or needs one. In Clean the reader never writes
`IO`: a function that calls `printf`, `scanf`, `fopen`, … or that calls such a
function, is emitted as `IO` automatically, and `main` always is (chapter 2).
The `#print` outputs and `IO.Error`'s constructors are Lean reflection:

```lean
#print Nat
#print String.toLower
#print IO
#print EIO
#print EST
#print EST.Out
#print EST.pure
#print EST.bind
```

---

## 4.6 Additional Conveniences

### Shared argument types

`def equal? [BEq α] (x : α) (y : α)` may be written `(x y : α)`. Clean does the
merge for every adjacent pair of same-typed C parameters (main-spec §7.1), so
the C spelling is the short one:

```c
typedef struct { bool ok; int val; } OptionInt;

OptionInt equalOpt(int x, int y) {              /* equal? */
  OptionInt r = {false, 0};
  if (x == y) {
    r.ok = true;
    r.val = x;
  }
  return r;
}
```

(`equal?` is generic in `α` with `[BEq α]` — G4, G20 — so this is its `Int`
instance, and the result is the `OptionInt` stand-in for `Option`, G1.)

### Leading dot — constructors without their namespace

`.leaf`, `.branch l x r` and the `BinTree.empty : BinTree α := .leaf` example
depend on recursive types (G3). `Weekday` is an enumeration, and C's enumerators
are already unqualified (Clean emits `open Weekday`), so the long and short
Lean spellings collapse into one C `switch`; stacked `case` labels are the
or-pattern `| .saturday | .sunday =>`:

```c
enum Weekday { monday, tuesday, wednesday, thursday, friday, saturday, sunday };

bool isWeekend(enum Weekday day) {
  switch (day) {
    case saturday:
    case sunday:   return true;
    default:       return false;
  }
}
```

### ❌ Or-patterns that bind variables, `Sum`-typed arguments (G2, G3)

The `condense`, `stringy`, `getTheNat` examples bind variables inside the
alternatives of an or-pattern over a `Sum` of pairs; that needs tagged unions
and tuples:

```lean
def BinTree.mirror : BinTree α → BinTree α
  | BinTree.leaf => BinTree.leaf
  | BinTree.branch l x r => BinTree.branch (mirror r) x (mirror l)

def BinTree.mirror : BinTree α → BinTree α
  | .leaf => .leaf
  | .branch l x r => .branch (mirror r) x (mirror l)

def BinTree.empty : BinTree α := .leaf

def condense : α ⊕ α → α
  | .inl x | .inr x => x

def stringy : Nat ⊕ Weekday → String
  | .inl x | .inr x => s!"It is {repr x}"

def getTheNat : (Nat × α) ⊕ (Nat × β) → Nat
  | .inl (n, x) | .inr (n, y) => n

def getTheAlpha : (Nat × α) ⊕ (Nat × α) → α
  | .inl (n, x) | .inr (n, y) => x    -- error: unknown identifier `x`

def str := "Some string"

def getTheString : (Nat × String) ⊕ (Nat × β) → String
  | .inl (n, str) | .inr (n, y) => str
```

### `do` with state — the last examples of §4.4

```lean
def increment : State Nat Nat := do
  let n ← get
  set (n + 1)
  pure n

def number (t : BinTree α) : BinTree (Nat × α) :=
  let rec helper : BinTree α → State Nat (BinTree (Nat × α))
    | BinTree.leaf => pure BinTree.leaf
    | BinTree.branch left x right => do
      pure
        (BinTree.branch
          (← helper left)
          ((← increment), x)
          (← helper right))
  (helper t 0).snd
```
