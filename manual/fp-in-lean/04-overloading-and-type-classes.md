# 3. Overloading and Type Classes — in Clean

Book chapter: <https://lean-lang.org/functional_programming_in_lean/Overloading-and-Type-Classes/>
Legend and gap IDs: see [README.md](README.md).

This chapter is the **widest gap** between C and Lean so far: type classes are
Lean's overloading mechanism, and C has none (C++ would, main-spec §15 🔮). Clean
therefore keeps the *data and the algorithms* of every example and drops the
*class machinery*. Overloaded operators (`+` on `Pos`) become named functions
(`Pos_plus`); instances, `outParam`s and coercions stay Lean-only (**G20**).

| Section | Status |
| --- | --- |
| 3.1 Positive Numbers | 🟡 `Pos` as a struct and named functions ✅; `class`/`instance`/`OfNat` ❌ G20 |
| 3.2 Type Classes and Polymorphism | 🟡 concrete instances ✅; `[Add α]` ❌ G4, G20 |
| 3.3 Controlling Instance Search | 🟡 the mixed-type adders ✅; `HAdd`, `outParam`, `default_instance` ❌ G20 |
| 3.4 Arrays and Indexing | 🟡 arrays ✅, `NonEmptyList` as fixed-size struct; `GetElem` ❌ G20, `Option` ❌ G1 |
| 3.5 Standard Classes | 🟡 comparison as `int`; `BEq`/`Hashable`/`Functor` ❌ G20, G3 |
| 3.6 Coercions | 🟡 explicit conversion functions ✅; `Coe*` ❌ G20 |
| 3.7 Additional Conveniences | ✅ structure instances; `instance … where` ❌ G20 |

---

## 3.1 Positive Numbers

The book defines `Pos` (`one | succ Pos`) and shows that numerals `7` and operators
`+`, `*` do not work on it until instances are written. The last version in the
chapter swaps in a *structure* with one field, which is the C-friendly shape:

```lean
structure Pos where
  succ ::
  pred : Nat
```

A `Pos` holds `n - 1` for the number `n ≥ 1`. In Clean the operators become named
functions (no overloading), and the "numeric literal `7`" is a brace initialiser:

```c
typedef struct { unsigned pred; } Pos;

Pos Pos_succ(Pos p) {
  Pos r = {p.pred + 1};
  return r;
}

unsigned Pos_toNat(Pos p) { return p.pred + 1; }

Pos Pos_plus(Pos n, Pos k) {           /* Pos.plus, the `+` of `instance : Add Pos` */
  Pos r = {n.pred + k.pred + 1};
  return r;
}

Pos Pos_mul(Pos n, Pos k) {            /* Pos.mul, the `*` of `instance : Mul Pos` */
  Pos r = {(n.pred + 1) * (k.pred + 1) - 1};
  return r;
}

const Pos seven = {6};                 /* def seven : Pos := 7 */
const Pos fourteen = Pos_plus(seven, seven);
const Pos fortyNine = Pos_mul(seven, seven);

printf("There are %d", Pos_toNat(seven));    /* "There are 7" via `ToString Pos` */
```

`toString`/`ToString Pos` is `printf` with `Pos_toNat`: Clean has no
`ToString` class, but every `printf("%d", …)` argument is already a number.

### ❌ The inductive `Pos` and the classes `Plus`, `Add`, `Mul`, `ToString`, `OfNat` (G3, G20)

The book first builds `Pos` as a recursive `inductive` (`one`, `succ`), which
needs constructors with payloads (G3), then makes arithmetic work by declaring
instances — so that `seven + seven` and the literal `7` elaborate:

```lean
inductive Pos : Type where
  | one : Pos
  | succ : Pos → Pos

def seven : Pos :=
  Pos.succ (Pos.succ (Pos.succ (Pos.succ (Pos.succ (Pos.succ Pos.one)))))

class Plus (α : Type) where
  plus : α → α → α

instance : Plus Nat where
  plus := Nat.add

open Plus (plus)

def Pos.plus : Pos → Pos → Pos
  | Pos.one, k => Pos.succ k
  | Pos.succ n, k => Pos.succ (n.plus k)

instance : Plus Pos where
  plus := Pos.plus

def fourteen : Pos := plus seven seven

instance : Add Pos where
  add := Pos.plus

def fourteen : Pos := seven + seven

def posToString (atTop : Bool) (p : Pos) : String :=
  let paren s := if atTop then s else "(" ++ s ++ ")"
  match p with
  | Pos.one => "Pos.one"
  | Pos.succ n => paren s!"Pos.succ {posToString false n}"

instance : ToString Pos where
  toString := posToString true

def Pos.toNat : Pos → Nat
  | Pos.one => 1
  | Pos.succ n => n.toNat + 1

instance : ToString Pos where
  toString x := toString (x.toNat)

def Pos.mul : Pos → Pos → Pos
  | Pos.one, k => k
  | Pos.succ n, k => n.mul k + k

instance : Mul Pos where
  mul := Pos.mul

#eval [seven + seven, seven * seven, seven + seven]   -- [7, 49, 14]

class Zero (α : Type) where
  zero : α

class One (α : Type) where
  one : α

instance : One Pos where
  one := Pos.one
```

### Numeric literals — `OfNat`

`OfNat` makes `7 : Pos` mean `Pos` from a number. C has literals only of its
built-in types; an `enum` is the nearest user-defined finite type, and its
enumerators are named, not numbered:

```c
enum LT4 { zero, one, two, three };           /* the book's LT4: 0 … 3 only */

const enum LT4 a = three;
```

`#eval (3 : LT4)` and the failure `#eval (4 : LT4)` ("failed to synthesize
`OfNat LT4 4`") disappear: C's `enum` lets you write `3` for an `int`, but
Clean's conversion `int → enum` is refused (a number may name no enumerator).

```lean
class OfNat (α : Type) (_ : Nat) where
  ofNat : α

inductive LT4 where
  | zero
  | one
  | two
  | three

instance : OfNat LT4 0 where
  ofNat := LT4.zero
instance : OfNat LT4 1 where
  ofNat := LT4.one
instance : OfNat LT4 2 where
  ofNat := LT4.two
instance : OfNat LT4 3 where
  ofNat := LT4.three

instance : OfNat Pos (n + 1) where
  ofNat :=
    let rec natPlusOne : Nat → Pos
      | 0 => Pos.one
      | k + 1 => Pos.succ (natPlusOne k)
    natPlusOne n

def eight : Pos := 8
```

The exercises of §3.1 (`Pos`'s `Mul`, `ToString`, a type of even numbers, `HTTP`
methods, `OfNat` for them) all need instances; only the *functions* translate:

```c
typedef struct { unsigned pred; } Pos;
const Pos eight = {7};
```

---

## 3.2 Type Classes and Polymorphism

A function whose type is a class constraint, `List.sumOfContents [Add α]
[OfNat α 0]`, is polymorphism (G4) plus constraints (G20). Its *instance* at a
concrete element type is an ordinary C function:

```c
unsigned sumOfContents(const unsigned xs[], unsigned n) {
  unsigned s = 0;
  for (unsigned i = 0; i < n; i++) s += xs[i];
  return s;
}

const unsigned fourNats[4] = {1, 2, 3, 4};
printf("%d", sumOfContents(fourNats, 4));       /* 10 */
```

One function per element type is the C price for not having `[Add α]` (the spec's
`GENERIC(T)` macro of §11.2, which infers `[Add T]` from `+`, is only a proposal):

```lean
def List.sumOfContents [Add α] [OfNat α 0] : List α → α
  | [] => 0
  | x :: xs => x + xs.sumOfContents

def List.sumOfContents [Add α] [Zero α] : List α → α
  | [] => 0
  | x :: xs => x + xs.sumOfContents

def fourNats : List Nat := [1, 2, 3, 4]
#eval fourNats.sumOfContents          -- 10

def fourPos : List Pos := [1, 2, 3, 4]
#eval fourPos.sumOfContents           -- failed to synthesize Zero Pos

structure PPoint (α : Type) where
  x : α
  y : α

instance [Add α] : Add (PPoint α) where
  add p1 p2 := { x := p1.x + p2.x, y := p1.y + p2.y }
```

---

## 3.3 Controlling Instance Search

The mixed-type additions `Nat + Pos` and `Pos + Nat` are two named functions,
which is just what C's lack of overloading forces (and what the book's
`addNatPos` / `addPosNat` helpers already are):

```c
typedef struct { unsigned pred; } Pos;

Pos addNatPos(unsigned n, Pos p) {
  Pos r = {p.pred + n};
  return r;
}

Pos addPosNat(Pos p, unsigned n) {
  Pos r = {p.pred + n};
  return r;
}
```

(The Lean recursions `| 0, p => p | n + 1, p => Pos.succ (addNatPos n p)` are
replaced by one addition, since `Pos` here is a number, not a unary chain.)

### ❌ `HAdd`, `HPlus`, `outParam`, `default_instance` (G20)

Heterogeneous operators and the rules that let Lean pick one are the subject of
the section. None has a C counterpart:

```lean
instance : HAdd Nat Pos Pos where
  hAdd := addNatPos

instance : HAdd Pos Nat Pos where
  hAdd := addPosNat

class HPlus (α : Type) (β : Type) (γ : Type) where
  hPlus : α → β → γ

instance : HPlus Nat Pos Pos where
  hPlus := addNatPos

#eval toString (HPlus.hPlus (3 : Pos) (5 : Nat))
-- typeclass instance problem is stuck

class HPlus (α : Type) (β : Type) (γ : outParam Type) where
  hPlus : α → β → γ

instance [Add α] : HPlus α α α where
  hPlus := Add.add

@[default_instance]
instance [Add α] : HPlus α α α where
  hPlus := Add.add
```

---

## 3.4 Arrays and Indexing

```c
typedef const char *Str;

const Str northernTrees[4] = {"sloe", "birch", "elm", "oak"};
```

`NonEmptyList α` (a head and a `List` tail) is a struct with a *fixed-size* array
tail (the size is not part of the Lean type, D13, but the C declaration needs
one). Both `idahoSpiders` and the safe lookup translate:

```c
typedef const char *Str;

typedef struct { Str head; Str tail[4]; } NonEmptyListStr;

const NonEmptyListStr idahoSpiders = {
  "Banded Garden Spider",
  { "Long-legged Sac Spider", "Wolf Spider", "Hobo Spider", "Cat-faced Spider" }
};

bool inBounds(NonEmptyListStr xs, unsigned i) { return i <= 4; }

Str NonEmptyListStr_get(NonEmptyListStr xs, unsigned i) {
  if (i == 0) return xs.head;
  return xs.tail[i - 1];           /* a[i - 1]!, panics when out of range */
}
```

🟡 **G12.** The array is a stand-in for `List α`; `inBounds` is a `Bool`
function, not the `Prop` the book uses, so its use as *evidence* (G18) is lost.

### ❌ `get?`, `GetElem`, instances for indexing (G1, G18, G20)

The `Option`-returning `get?`, the proof-taking `get`, the `GetElem` class that
makes `xs[i]` work for user types, and the theorems `atLeastThreeSpiders`
(`by decide`) are Lean-only:

```lean
structure NonEmptyList (α : Type) : Type where
  head : α
  tail : List α

def idahoSpiders : NonEmptyList String := {
  head := "Banded Garden Spider",
  tail := [
    "Long-legged Sac Spider",
    "Wolf Spider",
    "Hobo Spider",
    "Cat-faced Spider"
  ]
}

def NonEmptyList.get? : NonEmptyList α → Nat → Option α
  | xs, 0 => some xs.head
  | xs, n + 1 => xs.tail[n]?

abbrev NonEmptyList.inBounds (xs : NonEmptyList α) (i : Nat) : Prop :=
  i ≤ xs.tail.length

theorem atLeastThreeSpiders : idahoSpiders.inBounds 2 := by decide

theorem notSixSpiders : ¬idahoSpiders.inBounds 5 := by decide

def NonEmptyList.get (xs : NonEmptyList α)
    (i : Nat) (ok : xs.inBounds i) : α :=
  match i with
  | 0 => xs.head
  | n + 1 => xs.tail[n]

class GetElem
    (coll : Type)
    (idx : Type)
    (item : outParam Type)
    (inBounds : outParam (coll → idx → Prop)) where
  getElem : (c : coll) → (i : idx) → inBounds c i → item

instance : GetElem (NonEmptyList α) Nat α NonEmptyList.inBounds where
  getElem := NonEmptyList.get

instance : GetElem (List α) Pos α
    (fun list n => list.length > n.toNat) where
  getElem (xs : List α) (i : Pos) ok := xs[i.toNat]

instance : GetElem (PPoint α) Bool α (fun _ _ => True) where
  getElem (p : PPoint α) (i : Bool) _ :=
    if not i then p.x else p.y
```

---

## 3.5 Standard Classes

### Comparison

The book's `LT`/`LE` instances and the `Ordering`-returning `Ord` instance are
C's two idioms: boolean predicates and the `-1 / 0 / 1` comparison
(`strcmp`, `qsort`'s callback):

```c
typedef struct { unsigned pred; } Pos;

bool Pos_lt(Pos a, Pos b) { return a.pred < b.pred; }   /* instance : LT Pos */
bool Pos_le(Pos a, Pos b) { return a.pred <= b.pred; }  /* instance : LE Pos */

int Pos_comp(Pos a, Pos b) {                            /* Pos.comp : Ordering */
  if (a.pred < b.pred) return -1;                       /* Ordering.lt */
  if (a.pred > b.pred) return 1;                        /* Ordering.gt */
  return 0;                                             /* Ordering.eq */
}
```

🟡 `Ordering` (`lt | eq | gt`) is a built-in `inductive`; written as a C `enum`
it would work too (`enum Ordering { lt, eq, gt };`), but `compare`/`max`/`min`
over an `Ord` class remain class-based (G20).

### ❌ `BEq`, `Hashable`, `deriving`, `Append`, `Functor` (G3, G20)

`structure`s and `enum`s already `deriving Repr, BEq, Inhabited` in Clean
(main-spec §4.5); the *user-visible* equality on structs is not in C (compare
fields), and hashing, `Append`, `Functor` and `BinTree` are class- or
recursion-based:

```lean
instance : LT Pos where
  lt x y := LT.lt x.toNat y.toNat

instance : LE Pos where
  le x y := LE.le x.toNat y.toNat

def Pos.comp : Pos → Pos → Ordering
  | Pos.one, Pos.one => Ordering.eq
  | Pos.one, Pos.succ _ => Ordering.lt
  | Pos.succ _, Pos.one => Ordering.gt
  | Pos.succ n, Pos.succ k => comp n k

instance : Ord Pos where
  compare := Pos.comp

class Hashable (α : Type) where
  hash : α → UInt64

def hashPos : Pos → UInt64
  | Pos.one => 0
  | Pos.succ n => mixHash 1 (hashPos n)

instance : Hashable Pos where
  hash := hashPos

instance [Hashable α] : Hashable (NonEmptyList α) where
  hash xs := mixHash (hash xs.head) (hash xs.tail)

inductive BinTree (α : Type) where
  | leaf : BinTree α
  | branch : BinTree α → α → BinTree α → BinTree α

def eqBinTree [BEq α] : BinTree α → BinTree α → Bool
  | BinTree.leaf, BinTree.leaf =>
    true
  | BinTree.branch l x r, BinTree.branch l2 x2 r2 =>
    x == x2 && eqBinTree l l2 && eqBinTree r r2
  | _, _ =>
    false

instance [BEq α] : BEq (BinTree α) where
  beq := eqBinTree

def hashBinTree [Hashable α] : BinTree α → UInt64
  | BinTree.leaf =>
    0
  | BinTree.branch left x right =>
    mixHash 1
      (mixHash (hashBinTree left)
        (mixHash (hash x)
          (hashBinTree right)))

instance [Hashable α] : Hashable (BinTree α) where
  hash := hashBinTree

deriving instance BEq, Hashable for Pos
deriving instance BEq, Hashable for NonEmptyList

class HAppend (α : Type) (β : Type) (γ : outParam Type) where
  hAppend : α → β → γ

instance : Append (NonEmptyList α) where
  append xs ys :=
    { head := xs.head, tail := xs.tail ++ ys.head :: ys.tail }

instance : HAppend (NonEmptyList α) (List α) (NonEmptyList α) where
  hAppend xs ys :=
    { head := xs.head, tail := xs.tail ++ ys }

instance : Functor NonEmptyList where
  map f xs := { head := f xs.head, tail := f <$> xs.tail }

instance : Functor PPoint where
  map f p := { x := f p.x, y := f p.y }

def concat [Append α] (xs : NonEmptyList α) : α :=
  let rec catList (start : α) : List α → α
    | [] => start
    | (z :: zs) => catList (start ++ z) zs
  catList xs.head xs.tail

class Functor (f : Type → Type) where
  map : {α β : Type} → (α → β) → f α → f β
  mapConst {α β : Type} (x : α) (coll : f β) : f α :=
    map (fun _ => x) coll
```

---

## 3.6 Coercions

C converts between arithmetic types implicitly; Clean makes the conversion
**explicit** in the generated Lean, choosing it from the types as written
(main-spec §5.3, D19). Between *user* types the conversion is a function you call
yourself — the book's `Coe Pos Nat` is `Pos_toNat`:

```c
typedef struct { unsigned pred; } Pos;

unsigned Pos_toNat(Pos p) { return p.pred + 1; }

int oneInt(Pos p) { return Pos_toNat(p); }     /* def oneInt : Int := Pos.one — unsigned → int is explicit in the Lean */
```

### ❌ `Coe`, `CoeDep`, `CoeSort`, `CoeFun`, `Option` auto-wrapping, JSON (G1, G3, G5, G20)

Chains of coercions (`Option (Option (Option String))` from a literal), dependent
coercions, coercions to `Type` and to *functions*, and the JSON serialiser built
on them are class- and function-value-based:

```lean
instance : Coe Pos Nat where
  coe x := x.toNat

#eval [1, 2, 3, 4].drop (2 : Pos)          -- [3, 4]

def oneInt : Int := Pos.one

inductive A where
  | a
inductive B where
  | b

instance : Coe A B where
  coe _ := B.b
instance : Coe B A where
  coe _ := A.a
instance : Coe Unit A where
  coe _ := A.a

def coercedToB : B := ()

def List.last? : List α → Option α
  | [] => none
  | [x] => x
  | _ :: x :: xs => last? (x :: xs)

def perhapsPerhapsPerhaps : Option (Option (Option String)) :=
  "Please don't tell me"

def perhapsPerhapsPerhapsNat : Option (Option (Option Nat)) :=
  ↑(392 : Nat)

instance : Coe (NonEmptyList α) (List α) where
  coe
    | { head := x, tail := xs } => x :: xs

class CoeDep (α : Type) (x : α) (β : Type) where
  coe : β

instance : CoeDep (List α) (x :: xs) (NonEmptyList α) where
  coe := { head := x, tail := xs }

structure Monoid where
  Carrier : Type
  neutral : Carrier
  op : Carrier → Carrier → Carrier

def natMulMonoid : Monoid :=
  { Carrier := Nat, neutral := 1, op := (· * ·) }

def natAddMonoid : Monoid :=
  { Carrier := Nat, neutral := 0, op := (· + ·) }

def stringMonoid : Monoid :=
  { Carrier := String, neutral := "", op := String.append }

def listMonoid (α : Type) : Monoid :=
  { Carrier := List α, neutral := [], op := List.append }

def foldMap (M : Monoid) (f : α → M.Carrier) (xs : List α) : M.Carrier :=
  let rec go (soFar : M.Carrier) : List α → M.Carrier
    | [] => soFar
    | y :: ys => go (M.op soFar (f y)) ys
  go M.neutral xs

instance : CoeSort Monoid Type where
  coe m := m.Carrier

instance : CoeSort Bool Prop where
  coe b := b = true

class CoeFun (α : Type) (makeFunctionType : outParam (α → Type)) where
  coe : (x : α) → makeFunctionType x
```

### `CoeFun`: `Adder`

Applying a *structure* as if it were a function (`add5 3`) is C++'s `operator()`.
In C it is a named function:

```c
typedef struct { unsigned howMuch; } Adder;

const Adder add5 = {5};

unsigned Adder_apply(Adder a, unsigned n) { return n + a.howMuch; }

printf("%d", Adder_apply(add5, 3));        /* 8 */
```

```lean
structure Adder where
  howMuch : Nat

def add5 : Adder := ⟨5⟩

instance : CoeFun Adder (fun _ => Nat → Nat) where
  coe a := (· + a.howMuch)

#eval add5 3                                -- 8
```

### JSON — tagged union plus recursion (G3)

```lean
inductive JSON where
  | true : JSON
  | false : JSON
  | null : JSON
  | string : String → JSON
  | number : Float → JSON
  | object : List (String × JSON) → JSON
  | array : List JSON → JSON

structure Serializer where
  Contents : Type
  serialize : Contents → JSON

def Str : Serializer :=
  { Contents := String,
    serialize := JSON.string
  }

instance : CoeFun Serializer (fun s => s.Contents → JSON) where
  coe s := s.serialize

def buildResponse (title : String) (R : Serializer)
    (record : R.Contents) : JSON :=
  JSON.object [
    ("title", JSON.string title),
    ("status", JSON.number 200),
    ("record", R record)
  ]

def dropDecimals (numString : String) : String :=
  if numString.contains '.' then
    let noTrailingZeros := numString.dropEndWhile (· == '0')
    (noTrailingZeros.dropEndWhile (· == '.')).copy
  else numString

def String.separate (sep : String) (strings : List String) : String :=
  match strings with
  | [] => ""
  | x :: xs => String.join (x :: xs.map (sep ++ ·))

partial def JSON.asString (val : JSON) : String :=
  match val with
  | true => "true"
  | false => "false"
  | null => "null"
  | string s => "\"" ++ Lean.Json.escape s ++ "\""
  | number n => dropDecimals n.toString
  | object members =>
    let memberToString mem :=
      "\"" ++ Lean.Json.escape mem.fst ++ "\": " ++ asString mem.snd
    "{" ++ ", ".separate (members.map memberToString) ++ "}"
  | array elements =>
    "[" ++ ", ".separate (elements.map asString) ++ "]"
```

---

## 3.7 Additional Conveniences

Structure instances in three styles — anonymous constructor, field names, and
`where` — are C's positional and designated initialisers (all work, both are
checked by Clean):

```c
typedef const char *Str;

typedef struct { Str latinName; Str commonNames[2]; } Tree;

const Tree oak   = {"Quercus robur", {"common oak", "European oak"}};

const Tree birch = {.latinName = "Betula pendula",
                    .commonNames = {"silver birch", "warty birch"}};

Str displayName(Tree t) { return t.latinName; }      /* instance : Display Tree */
```

`class Display` / `instance : Display Tree := ⟨Tree.latinName⟩` (three equivalent
spellings of an instance) cannot be written (G20):

```lean
structure Tree : Type where
  latinName : String
  commonNames : List String

def oak : Tree :=
  ⟨"Quercus robur", ["common oak", "European oak"]⟩

def birch : Tree :=
  { latinName := "Betula pendula",
    commonNames := ["silver birch", "warty birch"]
  }

def sloe : Tree where
  latinName := "Prunus spinosa"
  commonNames := ["sloe", "blackthorn"]

class Display (α : Type) where
  displayName : α → String

instance : Display Tree :=
  ⟨Tree.latinName⟩

instance : Display Tree :=
  { displayName := Tree.latinName }

instance : Display Tree where
  displayName t := t.latinName
```

`example` (a definition that is checked but not named) has no C form; raw Lean
passes it through:

```c
LEAN_DECL("example (n : Nat) (k : Nat) : Bool := n + k == k + n");
```
