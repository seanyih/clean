# 7. Programming with Dependent Types — in Clean

Book chapter: <https://lean-lang.org/functional_programming_in_lean/Programming-with-Dependent-Types/>
Legend and gap IDs: see [README.md](README.md).

Dependent types let a *type* mention a *value*: `Vect α n` is a list whose type says
its length is `n`; `Row s` is a tuple whose shape is computed from the schema `s`.
C has no such types, and Clean maps every C array to an `Array` whose length is
**not** part of the type (decision D13; `Vector α n` is planned 🟡 for v0.2).
So this chapter is mostly **❌ undone**. Two things survive in C form:

* the *programs* — on fixed-size arrays with an explicit `n`, the length agreement the
  type used to guarantee is a convention the caller keeps (and a run-time `a[i]!`
  panic when it is broken);
* Clean's one genuine dependent type, **`bv(N)`**: the width is part of the type, and
  mixing widths is a translator error (**E301**) exactly like `Vect.zip` refusing
  lists of different lengths.

| Section | Status |
| --- | --- |
| 7.1 Indexed Families (`Vect`) | ❌ G7 `Vect α n`; the programs on arrays ✅ (C `n` parameter); `bv(N)` as the C-side analogue ✅ |
| 7.2 The Universe Design Pattern | ❌ G7, G3 — types computed from codes; `decode` split into two functions 🟡 |
| 7.3 Worked Example: Typed Queries | 🟡 the data, a *fixed-schema* query as loops ✅; the schema-indexed `Row`/`Query` ❌ G7, G3, G20 |
| 7.4 Indices, Parameters, and Universe Levels | ❌ G7 |
| 7.5 Pitfalls of Programming with Dependent Types | ❌ G7, G18 (rewriting with `▸`); the C `append` ✅ |
| 7.6 Summary | — |

---

## 7.1 Indexed Families

```lean
def natOrStringThree (b : Bool) : if b then Nat else String :=
  match b with
  | true => (3 : Nat)
  | false => "three"
```

A function whose *result type* depends on its argument has no C form: a C function
has one return type. (Clean's escape hatch `LEAN_EXPR` can carry any Lean
expression, but it needs the expected type at the use site — main-spec §11.1.)

### `Vect α n` — ❌ G7

```lean
inductive Vect (α : Type u) : Nat → Type u where
   | nil : Vect α 0
   | cons : α → Vect α n → Vect α (n + 1)

example : Vect String 3 :=
  .cons "one" (.cons "two" (.cons "three" .nil))

example : Vect String 3 := Vect.nil                      -- error: has type Vect ?α 0
example : Vect String n := Vect.nil                      -- error
example : Vect String n := Vect.cons "Hello" (Vect.cons "world" Vect.nil)   -- error

def Vect.replicate (n : Nat) (x : α) : Vect α n :=
  match n with
  | 0 => .nil
  | k + 1 => .cons x (replicate k x)

def List.replicate (n : Nat) (x : α) : List α :=
  match n with
  | 0 => []
  | k + 1 => x :: x :: replicate k x       -- the bug the type `Vect α n` would catch

def Vect.zip : Vect α n → Vect β n → Vect (α × β) n
  | .nil, .nil => .nil
  | .cons x xs, .cons y ys => .cons (x, y) (zip xs ys)
```

### The programs on arrays

`replicate` fills an array of `n` slots: a `void` function that writes one array
parameter is an **array writer** — Clean makes it return the updated array
(D20, main-spec §4.6). The "bug" of `List.replicate` (two cells per step) is a
loop that writes `out[i]` twice — nothing in C would flag it either.

`zip` of two same-length sequences takes the one shared `n`; the output array holds
pairs (a struct, G2). The book's "`zip` of lists of different lengths silently
truncates" is what `zip` does here if the caller lies about `n` (an out-of-range read
panics):

```c
typedef const char *Str;
typedef struct { Str fst; Str snd; } StrPair;

void replicate(int out[], unsigned n, int x) {
  for (unsigned i = 0; i < n; i++) out[i] = x;
}

void zip(StrPair out[], Str xs[], Str ys[], unsigned n) {
  for (unsigned i = 0; i < n; i++) {
    out[i].fst = xs[i];
    out[i].snd = ys[i];
  }
}

int main(void) {
  int a[3] = {0};
  replicate(a, 3, 7);          /* a := replicate a 3 7  — the writer is re-assigned */
  return 0;
}
```

(The book's example zips `["Mount Hood", "Mount Jefferson", "South Sister"]` with
`["Møllehøj", "Yding Skovhøj", "Ejer Bavnehøj"]`. `Str xs[]` with
`typedef const char *Str;` and plain `const char *xs[]` are the same `Array String`.)

### Clean's dependent type: `bv(N)`

The width `N` is part of the type, and operations whose widths disagree are
refused at translation time — the same guarantee `Vect.zip` gets from its shared
index `n`. (Not in the book; it is Clean's own use of "types that mention values",
main-spec §5.4.)

```c
bv(16) widen(bv(8) a)            { return bv_zext(16, a); }      /* setWidth 16 */
bv(16) cat(bv(8) hi, bv(8) lo)   { return bv_cat(hi, lo); }      /* hi ++ lo : BitVec (8 + 8) */
bv(8)  low(bv(16) x)             { return bv_ext(x, 7, 0); }     /* extractLsb' 0 8 */
```

<!-- clean: skip -->
```c
bv(16) bad(bv(8) a, bv(16) b) { return a + b; }   /* REJECTED, E301: bit-width mismatch */
```

---

## 7.2 The Universe Design Pattern — ❌ (G7, G3)

The pattern: a datatype of *codes* (`NatOrBool`) and a function `asType` from codes
to types, so that a function can return "an `α` that depends on the code".
C has neither types-as-values nor a result type that depends on a value. The nearest
C is one function per code:

```c
typedef struct { bool ok; unsigned val; } OptionNat;
typedef struct { bool ok; bool val; } OptionBool;

bool allDigits(const char *s) {
  if (strlen(s) == 0) return false;
  for (unsigned i = 0; i < strlen(s); i++)
    if (!isdigit(s[i])) return false;
  return true;
}

OptionNat decodeNat(const char *input) {            /* decode .nat input  =  input.toNat? */
  OptionNat r = {false, 0};
  if (!allDigits(input)) return r;
  r.ok = true;
  for (unsigned i = 0; i < strlen(input); i++) r.val = r.val * 10 + (input[i] - '0');
  return r;
}

OptionBool decodeBool(const char *input) {          /* decode .bool input */
  OptionBool r = {false, false};
  if (strcmp(input, "true") == 0) { r.ok = true; r.val = true; }
  else if (strcmp(input, "false") == 0) { r.ok = true; r.val = false; }
  return r;
}
```

The original — `decode`, nested-pair codes, and a universe of finite types with
*function types* inside (so equality of functions on `Bool` is decided by
enumerating all arguments):

```lean
inductive NatOrBool where
  | nat | bool

abbrev NatOrBool.asType (code : NatOrBool) : Type :=
  match code with
  | .nat => Nat
  | .bool => Bool

def decode (t : NatOrBool) (input : String) : Option t.asType :=
  match t with
  | .nat => input.toNat?
  | .bool =>
    match input with
    | "true" => some true
    | "false" => some false
    | _ => none

inductive NestedPairs where
  | nat : NestedPairs
  | pair : NestedPairs → NestedPairs → NestedPairs

abbrev NestedPairs.asType : NestedPairs → Type
  | .nat => Nat
  | .pair t1 t2 => asType t1 × asType t2

def NestedPairs.beq (t : NestedPairs) (x y : t.asType) : Bool :=
  match t with
  | .nat => x == y
  | .pair t1 t2 => beq t1 x.fst y.fst && beq t2 x.snd y.snd

instance {t : NestedPairs} : BEq t.asType where
  beq x y := t.beq x y

inductive Finite where
  | unit : Finite
  | bool : Finite
  | pair : Finite → Finite → Finite
  | arr : Finite → Finite → Finite

abbrev Finite.asType : Finite → Type
  | .unit => Unit
  | .bool => Bool
  | .pair t1 t2 => asType t1 × asType t2
  | .arr dom cod => asType dom → asType cod

def Finite.beq (t : Finite) (x y : t.asType) : Bool :=
  match t with
  | .unit => true
  | .bool => x == y
  | .pair t1 t2 => beq t1 x.fst y.fst && beq t2 x.snd y.snd
  | .arr dom cod =>
    dom.enumerate.all fun arg => beq cod (x arg) (y arg)

#eval Finite.beq (.arr .bool .bool) (fun _ => true) (fun b => b == b)
#eval Finite.beq (.arr .bool .bool) (fun _ => true) not
#eval Finite.beq (.arr .bool .bool) id (not ∘ not)

def Finite.enumerate (t : Finite) : List t.asType :=
  match t with
  | .unit => [()]
  | .bool => [true, false]
  | .pair t1 t2 => t1.enumerate.product t2.enumerate
  | .arr dom cod => dom.functions cod.enumerate

def List.product (xs : List α) (ys : List β) : List (α × β) := Id.run do
  let mut out : List (α × β) := []
  for x in xs do
    for y in ys do
      out := (x, y) :: out
  pure out.reverse

def Finite.functions
    (t : Finite)
    (results : List α) : List (t.asType → α) :=
  match t with
  | .unit =>
    results.map fun r =>
      fun () => r
  | .bool =>
    (results.product results).map fun (r1, r2) =>
      fun
        | true => r1
        | false => r2
  | .pair t1 t2 =>
    let f1s := t1.functions <| t2.functions results
    f1s.map fun f =>
      fun (x, y) =>
        f x y
  | .arr t1 t2 =>
    let args := t1.enumerate
    let base :=
      results.map fun r =>
        fun _ => r
    args.foldr
      (fun arg rest =>
        (t2.functions rest).map fun more =>
          fun f => more (f arg) f)
      base

def List.foldr (f : α → β → β) (default : β) : List α → β
  | []     => default
  | a :: l => f a (foldr f default l)
```

---

## 7.3 Worked Example: Typed Queries

The book builds a small relational-database library whose schema (a list of column
names and types) determines the *type* of a row, so that a query like
`select elevation where elevation > 500` is checked statically. Everything
type-level is ❌; the **data** and the **queries as C loops** over a fixed schema
are not.

The column types are an `enum`, a column is a struct, and the two tables of the
book are arrays of struct rows. Each table has its *own* row struct (this is
what `Row s` computes from `s`):

```c
typedef const char *Str;

enum DBType { dbInt, dbString, dbBool };           /* DBType.int | .string | .bool */

typedef struct { Str name; enum DBType contains; } Column;

const Column peakSchema[4] = {                     /* abbrev peak : Schema */
  {"name", dbString}, {"location", dbString}, {"elevation", dbInt}, {"lastVisited", dbInt}
};

typedef struct { Str name; Str location; int elevation; int lastVisited; } PeakRow;

const PeakRow mountainDiary[4] = {                 /* def mountainDiary : Table peak */
  {"Mount Nebo",       "USA",     3637, 2013},
  {"Moscow Mountain",  "USA",     1519, 2015},
  {"Himmelbjerget",    "Denmark",  147, 2004},
  {"Mount St. Helens", "USA",     2549, 2010}
};

typedef struct { Str name; Str location; int lastVisited; } WaterfallRow;

const WaterfallRow waterfallDiary[2] = {           /* def waterfallDiary : Table waterfall */
  {"Multnomah Falls", "USA", 2018},
  {"Shoshone Falls",  "USA", 2014}
};
```

A `DBExpr` such as `tallInDenmark` (`1000 < elevation && location == "Denmark"`)
is simply a C predicate, and the book's `example1` (`select` then `project` the
elevations > 500, giving `[3637, 1519, 2549]` in the book's table) and `example2`
(the cartesian product of both tables, selecting equal `location`) are loops:

<!-- clean: continue -->
```c
bool tallInDenmark(PeakRow r) {
  return 1000 < r.elevation && strcmp(r.location, "Denmark") == 0;
}

unsigned countTall(const PeakRow rows[], unsigned n) {
  unsigned c = 0;
  for (unsigned i = 0; i < n; i++)
    if (500 < rows[i].elevation) c++;
  return c;
}

void tallElevations(int out[], const PeakRow rows[], unsigned n) {   /* select … |>.project [elevation] */
  unsigned k = 0;
  for (unsigned i = 0; i < n; i++) {
    if (500 < rows[i].elevation) {
      out[k] = rows[i].elevation;
      k++;
    }
  }
}

unsigned countSameLocation(const PeakRow ms[], unsigned n,          /* product, then select (= location) */
                           const WaterfallRow ws[], unsigned m) {
  unsigned c = 0;
  for (unsigned i = 0; i < n; i++)
    for (unsigned j = 0; j < m; j++)
      if (strcmp(ms[i].location, ws[j].location) == 0) c++;
  return c;
}
```

What the C loses is everything the types proved: that the column named in a
projection exists with the right type (`HasCol`), that the projected schema is a
subset of the table's (`Subschema`), that the product's column names are disjoint.
A misspelled field is still a translator error (`unknown field`), but a *query
built at run time* from a schema value has no C counterpart.

### ❌ Schemas, rows and queries as types (G7, G3, G20)

```lean
inductive DBType where
  | int | string | bool

abbrev DBType.asType : DBType → Type
  | .int => Int
  | .string => String
  | .bool => Bool

def DBType.beq (t : DBType) (x y : t.asType) : Bool :=
  match t with
  | .int => x == y
  | .string => x == y
  | .bool => x == y

instance {t : DBType} : BEq t.asType where
  beq := t.beq

instance : BEq DBType where
  beq
    | .int, .int => true
    | .string, .string => true
    | .bool, .bool => true
    | _, _ => false

instance {t : DBType} : Repr t.asType where
  reprPrec :=
    match t with
    | .int => reprPrec
    | .string => reprPrec
    | .bool => reprPrec

structure Column where
  name : String
  contains : DBType

abbrev Schema := List Column

abbrev Row : Schema → Type
  | [] => Unit
  | [col] => col.contains.asType
  | col1 :: col2 :: cols => col1.contains.asType × Row (col2::cols)

abbrev Table (s : Schema) := List (Row s)

abbrev peak : Schema := [
  ⟨"name", .string⟩,
  ⟨"location", .string⟩,
  ⟨"elevation", .int⟩,
  ⟨"lastVisited", .int⟩
]

def mountainDiary : Table peak := [
  ("Mount Nebo",       "USA",     3637, 2013),
  ("Moscow Mountain",  "USA",     1519, 2015),
  ("Himmelbjerget",    "Denmark",  147, 2004),
  ("Mount St. Helens", "USA",     2549, 2010)
]

abbrev waterfall : Schema := [
  ⟨"name", .string⟩,
  ⟨"location", .string⟩,
  ⟨"lastVisited", .int⟩
]

def waterfallDiary : Table waterfall := [
  ("Multnomah Falls", "USA", 2018),
  ("Shoshone Falls",  "USA", 2014)
]

def Row.bEq (r1 r2 : Row s) : Bool :=
  match s with
  | [] => true
  | [_] => r1 == r2
  | _::_::_ =>
    match r1, r2 with
    | (v1, r1'), (v2, r2') =>
      v1 == v2 && bEq r1' r2'

instance : BEq (Row s) where
  beq := Row.bEq

inductive HasCol : Schema → String → DBType → Type where
  | here : HasCol (⟨name, t⟩ :: _) name t
  | there : HasCol s name t → HasCol (_ :: s) name t

def Row.get (row : Row s) (col : HasCol s n t) : t.asType :=
  match s, col, row with
  | [_], .here, v => v
  | _::_::_, .here, (v, _) => v
  | _::_::_, .there next, (_, r) => get r next

inductive Subschema : Schema → Schema → Type where
  | nil : Subschema [] bigger
  | cons :
      HasCol bigger n t →
      Subschema smaller bigger →
      Subschema (⟨n, t⟩ :: smaller) bigger

abbrev travelDiary : Schema :=
  [⟨"name", .string⟩, ⟨"location", .string⟩, ⟨"lastVisited", .int⟩]

example : Subschema travelDiary peak :=
  .cons .here
    (.cons (.there .here)
      (.cons (.there (.there (.there .here))) .nil))

example : Subschema travelDiary peak := by repeat constructor

def Subschema.addColumn :
    Subschema smaller bigger →
    Subschema smaller (c :: bigger)
  | .nil  => .nil
  | .cons col sub' => .cons (.there col) sub'.addColumn

def Subschema.reflexive : (s : Schema) → Subschema s s
  | [] => .nil
  | _ :: cs => .cons .here (reflexive cs).addColumn

def Row.project (row : Row s) : (s' : Schema) → Subschema s' s → Row s'
  | [], .nil => ()
  | [_], .cons c .nil => row.get c
  | _::_::_, .cons c cs => (row.get c, row.project _ cs)

inductive DBExpr (s : Schema) : DBType → Type where
  | col (n : String) (loc : HasCol s n t) : DBExpr s t
  | eq (e1 e2 : DBExpr s t) : DBExpr s .bool
  | lt (e1 e2 : DBExpr s .int) : DBExpr s .bool
  | and (e1 e2 : DBExpr s .bool) : DBExpr s .bool
  | const : t.asType → DBExpr s t

macro "c!" n:term : term => `(DBExpr.col $n (by repeat constructor))

def tallInDenmark : DBExpr peak .bool :=
  .and (.lt (.const 1000) (c! "elevation"))
    (.eq (c! "location") (.const "Denmark"))

def DBExpr.evaluate (row : Row s) : DBExpr s t → t.asType
  | .col _ loc => row.get loc
  | .eq e1 e2  => evaluate row e1 == evaluate row e2
  | .lt e1 e2  => evaluate row e1 < evaluate row e2
  | .and e1 e2 => evaluate row e1 && evaluate row e2
  | .const v => v

inductive Query : Schema → Type where
  | table : Table s → Query s
  | union : Query s → Query s → Query s
  | diff : Query s → Query s → Query s
  | select : Query s → DBExpr s .bool → Query s
  | project :
    Query s → (s' : Schema) →
    Subschema s' s →
    Query s'
  | product :
    Query s1 → Query s2 →
    disjoint (s1.map Column.name) (s2.map Column.name) →
    Query (s1 ++ s2)
  | renameColumn :
    Query s → (c : HasCol s n t) → (n' : String) →
    !((s.map Column.name).contains n') →
    Query (s.renameColumn c n')
  | prefixWith :
    (n : String) → Query s →
    Query (s.map fun c => {c with name := n ++ "." ++ c.name})

def disjoint [BEq α] (xs ys : List α) : Bool :=
  not (xs.any ys.contains || ys.any xs.contains)

def Schema.renameColumn : (s : Schema) → HasCol s n t → String → Schema
  | c :: cs, .here, n' => {c with name := n'} :: cs
  | c :: cs, .there next, n' => c :: renameColumn cs next n'

def addVal (v : c.contains.asType) (row : Row s) : Row (c :: s) :=
  match s, row with
  | [], () => v
  | c' :: cs, v' => (v, v')

def Row.append (r1 : Row s1) (r2 : Row s2) : Row (s1 ++ s2) :=
  match s1, r1 with
  | [], () => r2
  | [_], v => addVal v r2
  | _::_::_, (v, r') => (v, r'.append r2)

def Table.cartesianProduct (table1 : Table s1) (table2 : Table s2) :
    Table (s1 ++ s2) := Id.run do
  let mut out : Table (s1 ++ s2) := []
  for r1 in table1 do
    for r2 in table2 do
      out := (r1.append r2) :: out
  pure out.reverse

def List.without [BEq α] (source banned : List α) : List α :=
  source.filter fun r => !(banned.contains r)

def Row.rename (c : HasCol s n t) (row : Row s) :
    Row (s.renameColumn c n') :=
  match s, row, c with
  | [_], v, .here => v
  | _::_::_, (v, r), .here => (v, r)
  | _::_::_, (v, r), .there next => addVal v (r.rename next)

def prefixRow (row : Row s) :
    Row (s.map fun c => {c with name := n ++ "." ++ c.name}) :=
  match s, row with
  | [], _ => ()
  | [_], v => v
  | _::_::_, (v, r) => (v, prefixRow r)

def Query.exec : Query s → Table s
  | .table t => t
  | .union q1 q2 => exec q1 ++ exec q2
  | .diff q1 q2 => exec q1 |>.without (exec q2)
  | .select q e => exec q |>.filter e.evaluate
  | .project q _ sub => exec q |>.map (·.project _ sub)
  | .product q1 q2 _ => exec q1 |>.cartesianProduct (exec q2)
  | .renameColumn q c _ _ => exec q |>.map (·.rename c)
  | .prefixWith _ q => exec q |>.map prefixRow

open Query in
def example1 :=
  table mountainDiary |>.select
  (.lt (.const 500) (c! "elevation")) |>.project
  [⟨"elevation", .int⟩] (by repeat constructor)

open Query in
def example2 :=
  let mountain := table mountainDiary |>.prefixWith "mountain"
  let waterfall := table waterfallDiary |>.prefixWith "waterfall"
  mountain.product waterfall (by decide)
    |>.select (.eq (c! "mountain.location") (c! "waterfall.location"))
    |>.project [⟨"mountain.name", .string⟩, ⟨"waterfall.name", .string⟩]
      (by repeat constructor)

structure NDBType where
  underlying : DBType
  nullable : Bool

abbrev NDBType.asType (t : NDBType) : Type :=
  if t.nullable then
    Option t.underlying.asType
  else
    t.underlying.asType
```

---

## 7.4 Indices, Parameters, and Universe Levels — ❌ (G7)

*Parameters* (before the colon) are the same in every constructor; *indices* (after
it) may vary. The classification decides how many universes a type needs. Pure type
theory — no C:

```lean
inductive WithParameter (α : Type u) : Type u where
  | test : α → WithParameter α

inductive WithTwoParameters (α : Type u) (β : Type v) : Type (max u v) where
  | test : α → β → WithTwoParameters α β

inductive WithParameterAfterColon : Type u → Type u where
  | test : α → WithParameterAfterColon α

inductive WithParameterAfterColon2 : Type u → Type u where
  | test1 : α → WithParameterAfterColon2 α
  | test2 : WithParameterAfterColon2 α

inductive WithNamedIndex (α : Type u) : Type (u + 1) where
  | test1 : WithNamedIndex α
  | test2 : WithNamedIndex α → WithNamedIndex α → WithNamedIndex (α × α)

inductive ParamAfterIndex : Nat → Type u → Type u where
  | test1 : ParamAfterIndex 0 γ
  | test2 : ParamAfterIndex n γ → ParamAfterIndex k γ → ParamAfterIndex (n + k) γ

inductive NatParam (n : Nat) : Nat → Type u where
  | five : NatParam 4 5
```

---

## 7.5 Pitfalls of Programming with Dependent Types

`Vect`'s `append` has to prove that its result has length `n + k`, which depends on
*how* `+` is defined (recursion on its left argument or its right), and the
mismatch forces rewrites with `▸` and auxiliary lemmas. In C, `append` is two loops
and the caller allocates `n + k` slots; no proof exists to be convenient or awkward:

```c
void append(int out[], const int xs[], unsigned n, const int ys[], unsigned k) {
  for (unsigned i = 0; i < n; i++) out[i] = xs[i];
  for (unsigned j = 0; j < k; j++) out[n + j] = ys[j];
}
```

(`out` is the single written array parameter, so `append` is a D20 writer: the
call is `out := append out xs n ys k`.)

The Lean side — the two `plus`es, the three attempts at `appendR`, the lemmas
`plusR_zero_left` and `plusR_succ_left`, and the final version using `▸` — is G7 +
G18:

```lean
def Nat.plusL : Nat → Nat → Nat
  | 0, k => k
  | n + 1, k => plusL n k + 1

def Nat.plusR : Nat → Nat → Nat
  | n, 0 => n
  | n, k + 1 => plusR n k + 1

def appendL : (n k : Nat) → Vect α n → Vect α k → Vect α (n.plusL k)
  | 0, k, .nil, ys => ys
  | n + 1, k, .cons x xs, ys => .cons x (appendL n k xs ys)

def appendL : Vect α n → Vect α k → Vect α (n.plusL k)
  | .nil, ys => ys
  | .cons x xs, ys => .cons x (appendL xs ys)

theorem plusR_zero_left : (k : Nat) → k = Nat.plusR 0 k
  | 0 => by rfl
  | k + 1 =>
    congrArg (· + 1) (plusR_zero_left k)

theorem plusR_succ_left (n : Nat) :
    (k : Nat) → Nat.plusR (n + 1) k = Nat.plusR n k + 1
  | 0 => by rfl
  | k + 1 => congrArg (· + 1) (plusR_succ_left n k)

def appendR : (n k : Nat) → Vect α n → Vect α k → Vect α (n.plusR k)
  | 0, k, .nil, ys =>
    plusR_zero_left k ▸ ys
  | n + 1, k, .cons x xs, ys =>
    plusR_succ_left n k ▸ .cons x (appendR n k xs ys)

def appendR : Vect α n → Vect α k → Vect α (n.plusR k)
  | .nil, ys => plusR_zero_left _ ▸ ys
  | .cons x xs, ys => plusR_succ_left _ _ ▸ .cons x (appendR xs ys)
```

---

## 7.6 Summary

Dependent types are the book's bridge from programs to proofs; Clean deliberately
stops at the bridge's C side: `bv(N)` for width-indexed values, `VERIFY_FORALL`
for properties of finite-width functions (see the interlude on propositions and
the one on tactics), and `LEAN_DECL` for anything beyond.
