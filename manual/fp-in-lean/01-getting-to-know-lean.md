# 1. Getting to Know Lean — in Clean

Book chapter: <https://lean-lang.org/functional_programming_in_lean/Getting-to-Know-Lean/>
Legend and gap IDs (`G1`, `G2`, …): see [README.md](README.md).

| Section | Status |
| --- | --- |
| 1.1 Evaluating Expressions | ✅ (partial application ❌ G5) |
| 1.2 Types | ✅ / `#check` 🟡 via raw Lean |
| 1.3 Functions and Definitions | ✅ (`def` vs `abbrev` for types 🟡 G9) |
| 1.4 Structures | ✅ incl. compound literals (methods / function-valued fields ❌ G5, custom constructor name ❌ G10) |
| 1.5 Datatypes and Patterns | 🟡 enums, `switch` and recursion ✅; constructors with payloads ❌ G3 |
| 1.6 Polymorphism | ❌ G4 `List`/`Option`/`Prod`/`Sum` are written out as Lean; concrete C stand-ins given where possible |
| 1.7 Characters, Strings, and Slices | 🟡 `strlen`, `<ctype.h>`; slices ❌ G8 |
| 1.8 Additional Conveniences | 🟡 `switch`, namespaces ✅; the rest ❌ |

Every Clean block in this manual is compiled by `release/clean` (see `check.py`).
A block marked `<!-- clean: continue -->` extends the block above it.

---

## 1.1 Evaluating Expressions

A bare `printf` at file scope is Lean's `#eval` (main-spec §8.2): it is evaluated
when Lean checks the file, not when a program runs.

```c
printf("%d", 1 + 2);              /* #eval 1 + 2       → 3  */
printf("%d", 1 + 2 * 5);          /* #eval 1 + 2 * 5   → 11 */
printf("%s", "Hello, Lean!");     /* a string value is printed quoted by #eval */
```

`String.append` is `strcat`, which Clean accepts **only as a statement** (it
appends to a local string variable). The book's `#eval String.append "Hello, "
"Lean!"` and `#eval String.append "great " (String.append "oak " "tree")`:

```c
const char *greeting(void) {
  char s[] = "Hello, ";
  strcat(s, "Lean!");
  return s;
}

const char *greatOak(void) {
  char s[] = "great ";
  strcat(s, "oak ");
  strcat(s, "tree");
  return s;
}

puts(greeting());     /* "Hello, Lean!" */
puts(greatOak());     /* "great oak tree" */
```

The book's `String.append "it is " (if 1 > 2 then "yes" else "no")`
(the conditional is evaluated before the append):

```c
const char *itIs(int a, int b) {
  char r[] = "it is ";
  const char *w = a > b ? "yes" : "no";
  strcat(r, w);
  return r;
}
puts(itIs(1, 2));   /* "it is no" */
```

### ❌ Partial application is an error that C cannot even write (G5)

The book shows that `#eval String.append "it is "` fails because the result is
a function, which has no `Repr`. C has no partially applied functions, so there
is nothing to translate (a call with too few arguments is a C error).

```lean
#eval String.append "it is "
-- Could not synthesize a `ToExpr`, `Repr`, or `ToString` instance for type
--   String → String
```

---

## 1.2 Types

`Nat` is `unsigned`, `Int` is `int` (decided in design-decisions.md). `Nat`
subtraction truncates at zero, the first Lean surprise a C programmer meets
(lint W002 reports it). The book's `#eval (1 + 2 : Nat)`, `(1 - 2 : Nat)` and
`(1 - 2 : Int)`:

```c
unsigned addNat(unsigned a, unsigned b) { return a + b; }
unsigned subNat(unsigned a, unsigned b) { return a - b; }
int      subInt(int a, int b)           { return a - b; }

printf("%d", addNat(1, 2));       /* 3 */
printf("%d", subNat(1, 2));       /* 0  (Nat truncates) */
printf("%d", subInt(1, 2));       /* -1 */
```

`#check` has no C spelling, but the raw-Lean escape hatch passes it through
(🟡: it works, it is just not C):

```c
LEAN_DECL("#check (1 - 2 : Int)");
```

### ❌ A type error is an error in C too, but a different one (G26)

`#check String.append ["hello", " "] "world"` is the book's example of Lean
refusing a list where a `String` is expected. In Clean the equivalent mistakes
(`bool` for `int`, `String` for an array, …) are refused by the translator itself
(error codes in `spec/error-codes.md`); there is nothing to write.

```lean
#check String.append ["hello", " "] "world"
```

---

## 1.3 Functions and Definitions

Constants are `const` globals (main-spec §7.3); functions are functions.

```c
const char *hello = "Hello";
const char *lean  = "Lean";

int add1(int n) { return n + 1; }

unsigned maximum(unsigned n, unsigned k) { return n < k ? k : n; }
printf("%d", maximum(5 + 8, 2 * 7));        /* 14 */
```

The same `maximum` with an `if` statement instead of `?:` (both are accepted):

```c
unsigned maximum(unsigned n, unsigned k) {
  if (n < k) return k;
  else return n;
}
```

The book's `spaceBetween`, used by `#eval String.append hello (String.append " " lean)`:

```c
const char *spaceBetween(const char *before, const char *after) {
  char r[] = "";
  strcat(r, before);
  strcat(r, " ");
  strcat(r, after);
  return r;
}
```

Exercises of §1.3.1:

```c
const char *joinStringsWith(const char *sep, const char *a, const char *b) {
  char r[] = "";
  strcat(r, a);
  strcat(r, sep);
  strcat(r, b);
  return r;
}
unsigned volume(unsigned height, unsigned width, unsigned depth) {
  return height * width * depth;
}
puts(joinStringsWith(", ", "one", "and another"));   /* "one, and another" */
```

> The book's `#check maximum 3 : Nat → Nat` and "all functions take exactly one
> argument (currying)" have **no C counterpart**: every Clean function takes all
> its arguments (see G5).

### 1.3.2 Defining Types — `typedef`

```c
typedef const char *Str;
const Str aStr = "This is a string.";

typedef unsigned NaturalNumber;
const NaturalNumber thirtyEight = 38;
```

🟡 **G9.** Clean's `typedef` is always Lean's `abbrev`. The book first uses
`def NaturalNumber : Type := Nat`, where `thirtyEight : NaturalNumber := 38`
*fails* ("failed to synthesize `OfNat NaturalNumber 38`") until the type is made
reducible (`abbrev N : Type := Nat`). With `typedef` you always get the working
behaviour; the failing `def` variant cannot be written:

```lean
def NaturalNumber : Type := Nat

def thirtyEight : NaturalNumber := 38   -- error: failed to synthesize OfNat NaturalNumber 38
def thirtyEight : NaturalNumber := (38 : Nat)   -- works
```

---

## 1.4 Structures

```c
typedef struct { double x; double y; } Point;

const Point origin = {0.0, 0.0};
printf("%f", origin.x);                 /* #eval origin.x → 0.000000 */

Point addPoints(Point p1, Point p2) {
  return (Point){p1.x + p2.x, p1.y + p2.y};      /* a C99 compound literal */
}

double distance(Point p1, Point p2) {
  return sqrt(pow(p2.x - p1.x, 2.0) + pow(p2.y - p1.y, 2.0));
}

typedef struct { double x; double y; double z; } Point3D;
const Point3D origin3D = {0.0, 0.0, 0.0};
```

The book's `{ x := 1.5, y := 32 }` becomes a C initialiser; both positional
(`{1.5, 32}`) and designated (`{.x = 1.5, .y = 32}`) forms work:

<!-- clean: continue -->
```c
const Point fourAndThree = {.x = 4.3, .y = 3.4};
```

The book's `#eval distance { x := 1.0, y := 2.0 } { x := 5.0, y := -1.0 }` and
`#eval addPoints { x := 1.5, y := 32 } { x := -8, y := 0.2 }` build structures in
place; in C that is a **compound literal** `(Point){…}` (positional or designated).
Clean emits `(⟨1.0, 2.0⟩ : Point)`, with the type written so that Lean never has
to guess it:

<!-- clean: continue -->
```c
printf("%f", distance((Point){1.0, 2.0}, (Point){5.0, -1.0}));     /* 5.000000 */
printf("%f", addPoints((Point){1.5, 32}, (Point){-8, 0.2}).x);     /* -6.500000 */
```

(✅ since gap **G6** was closed; before, the values had to be declared as
variables first.)

### Updating a structure

`{ p with x := 0 }` is a field assignment on a (local copy of a) struct:

<!-- clean: continue -->
```c
Point zeroX(Point p) {
  p.x = 0;
  return p;
}
```

Writing `Point zeroX(Point p) { Point q = {0, p.y}; return q; }` is the book's
first version (`{ x := 0, y := p.y }`); both translate.

### `#check` on literals, struct-valued `#eval`

`#check 1.2`, `#check -454.2123215`, `#check 0.0`, `#check 0` and `#check (0 : Float)`
show how a literal gets its type (`Float` for a decimal point, `Nat` otherwise;
a C literal always has its C type, so there is no such question).

`#eval fourAndThree` prints a whole structure; `printf` cannot print a struct,
so use raw Lean (🟡):

<!-- clean: continue -->
```c
LEAN_DECL("#eval fourAndThree");              /* { x := 4.300000, y := 3.400000 } */
LEAN_DECL("#check ({ x := 0.0, y := 0.0 } : Point)");
```

### Records with strings and booleans

<!-- clean: continue -->
```c
typedef struct { const char *name; bool fluffy; } Hamster;
typedef struct { const char *title; const char *author; double price; } Book;
```

### ❌ Custom constructor names, methods, function-valued fields (G10, G5)

`structure Point where point :: …` renames the constructor `Point.mk`; Clean
always uses the anonymous constructor `⟨ , ⟩` and brace initialisers, so there
is nothing to write. `Point.modifyBoth` takes a *function* `Float → Float`, and
`fourAndThree.modifyBoth Float.floor` calls it with dot syntax — C has neither
function-typed parameters in Clean (**E107**, G5) nor method syntax. A plain
C function that fixes the function (`floor`) is the available workaround:

<!-- clean: continue -->
```c
Point floorBoth(Point p) { return (Point){floor(p.x), floor(p.y)}; }
```

(The design for methods and function parameters in C++ style —
`Point modifyBoth(std::function<double(double)> f) const` — is in
[spec/cpp-front-end-design.md](../../spec/cpp-front-end-design.md) §3.1, §9.)

```lean
structure Point where
  point ::
  x : Float
  y : Float

def Point.modifyBoth (f : Float → Float) (p : Point) : Point :=
  { x := f p.x, y := f p.y }

#eval fourAndThree.modifyBoth Float.floor
-- { x := 4.000000, y := 3.000000 }
```

---

## 1.5 Datatypes and Patterns

### Enumerations

`inductive Firewood` / `WoodSplittingTool` (constructors without arguments) are
C `enum`s, and `match` on them is `switch`:

```c
enum WoodSplittingTool { axe, maul, froe };

const char *toolName(enum WoodSplittingTool t) {
  switch (t) {
    case axe:  return "axe";
    case maul: return "maul";
    case froe: return "froe";
  }
}
```

(An exhaustive `switch` needs no `default` and no trailing `return`.)

### Pattern matching on `Nat`

The book defines `Nat` as `zero | succ n` and matches with `Nat.zero` /
`Nat.succ k`. In C the pattern `succ k` is `n - 1` after testing `n != 0`:

<!-- clean: continue -->
```c
bool isZero(unsigned n) {
  switch (n) {
    case 0:  return true;
    default: return false;
  }
}

unsigned pred(unsigned n) {
  switch (n) {
    case 0:  return 0;
    default: return n - 1;
  }
}
```

🟡 **G11.** The binding `k` of `Nat.succ k` does not exist: it is written
`n - 1`. Patterns `n + 1` / `n + 2` (see 1.8) likewise become `n - 1` / `n - 2`.

### Matching a structure

`match p with | { x := h, y := w, z := d } => d` is a field access:

```c
typedef struct { double x; double y; double z; } Point3D;
double depth(Point3D p) { return p.z; }
```

### Recursive functions

```c
unsigned pred(unsigned n) { return n == 0 ? 0 : n - 1; }   /* as above */

bool even(unsigned n) {
  if (n == 0) return true;
  return !even(n - 1);
}

unsigned plus(unsigned n, unsigned k) {
  if (k == 0) return n;
  return plus(n, k - 1) + 1;
}

unsigned times(unsigned n, unsigned k) {
  if (k == 0) return 0;
  return plus(n, times(n, k - 1));
}

unsigned minus(unsigned n, unsigned k) {
  if (k == 0) return n;
  return pred(minus(n, k - 1));
}
```

The book's cautionary `evenLoops` (Lean cannot prove it terminates) is written
with the `partial` attribute (`[[lean::partial]]`, main-spec §7.1):

<!-- clean: continue -->
```c
[[lean::partial]] bool evenLoops(unsigned n) {
  if (n == 0) return true;
  return !evenLoops(n);
}
```

Note that Clean itself never checks termination; Lean does, and rejects such a
definition unless it is marked `partial`
(see `test/cases-lean-rejects/`).

### ❌ Constructors with payloads — `inductive` types that carry data (G3)

`Pos`-style recursion over a user-defined `inductive` is the heart of the book.
Constructors with arguments (`Nat.succ n`, `List.cons x xs`) need tagged unions
or recursive pointer structures, which Clean refuses (**E104** union, **E101**
pointers). Their original Lean:

```lean
inductive Bool where
  | false : Bool
  | true : Bool

inductive Nat where
  | zero : Nat
  | succ (n : Nat) : Nat

def isZero (n : Nat) : Bool :=
  match n with
  | Nat.zero => true
  | Nat.succ k => false

def pred (n : Nat) : Nat :=
  match n with
  | Nat.zero => Nat.zero
  | Nat.succ k => k

def even (n : Nat) : Bool :=
  match n with
  | Nat.zero => true
  | Nat.succ k => not (even k)

def plus (n : Nat) (k : Nat) : Nat :=
  match k with
  | Nat.zero => n
  | Nat.succ k' => Nat.succ (plus n k')
```

The evaluation traces the book prints for `plus 3 2` (rewriting by `match`
steps) are explanations, not code.

---

## 1.6 Polymorphism

Clean has **no generics** (G4; the `GENERIC(T)` macro of main-spec §11.2 is only
a proposal). Everything in this section that is parameterised by a type is
therefore left as the original Lean. Where a *concrete* C instance exists it is
given first.

### `PPoint α`

```lean
structure PPoint (α : Type) where
  x : α
  y : α

def natOrigin : PPoint Nat :=
  { x := Nat.zero, y := Nat.zero }

def replaceX (α : Type) (point : PPoint α) (newX : α) : PPoint α :=
  { point with x := newX }

#check (replaceX)
#check replaceX Nat
#check replaceX Nat natOrigin
#check replaceX Nat natOrigin 5
#eval replaceX Nat natOrigin 5
-- { x := 5, y := 0 }
```

The concrete C instance (`α := Nat`) — one struct per element type:

```c
typedef struct { unsigned x; unsigned y; } NatPoint;
const NatPoint natOrigin = {0, 0};

NatPoint replaceX(NatPoint point, unsigned newX) {
  point.x = newX;
  return point;
}
```

Explicit type arguments (`replaceX Nat natOrigin 5`) and implicit ones
(`{α : Type}`, the later `replaceX natOrigin 5`) have no C form at all:

```lean
def replaceX {α : Type} (point : PPoint α) (newX : α) : PPoint α :=
  { point with x := newX }
```

### Linked data: `List`

The book builds `List α` as `nil | cons`. The C analogue in Clean is an **array
plus a length** (decision D13: a C array is an `Array`, the size is not part of
the type). `List`-specific syntax (`[2, 3, 5, 7]`, `::`, `List.cons`) therefore
maps to array literals and index loops:

```c
const unsigned primesUnder10[4] = {2, 3, 5, 7};

unsigned length(const unsigned xs[], unsigned n) {
  unsigned count = 0;
  for (unsigned i = 0; i < n; i++) count++;
  return count;
}
```

🟡 **G12:** this is `Array Nat`, not `List Nat`; `length` is not structurally
recursive, `drop`/`head?`/`last?` on lists are index arithmetic. The original:

```lean
inductive List (α : Type) where
  | nil : List α
  | cons : α → List α → List α

def primesUnder10 : List Nat := [2, 3, 5, 7]

def explicitPrimesUnder10 : List Nat :=
  List.cons 2 (List.cons 3 (List.cons 5 (List.cons 7 List.nil)))

def length (α : Type) (xs : List α) : Nat :=
  match xs with
  | List.nil => Nat.zero
  | List.cons y ys => Nat.succ (length α ys)

def length (α : Type) (xs : List α) : Nat :=
  match xs with
  | [] => 0
  | y :: ys => Nat.succ (length α ys)

def length {α : Type} (xs : List α) : Nat :=
  match xs with
  | [] => 0
  | y :: ys => Nat.succ (length ys)
```

### ❌ `Option` (G1)

The "may not exist" type has no Clean type. The spec lists `Option`/`Except` as
🟡 for v0.2 (error-code returns such as `-1`/`NULL`). Today the only available
idiom is a hand-written struct plus an index function:

```c
typedef struct { bool some; int val; } OptionInt;

OptionInt headOpt(const int xs[], unsigned n) {
  OptionInt r = {false, 0};
  if (n > 0) { r.some = true; r.val = xs[0]; }
  return r;
}
```

The book's definition, `List.head?`, and its uses:

```lean
inductive Option (α : Type) : Type where
  | none : Option α
  | some (val : α) : Option α

def List.head? {α : Type} (xs : List α) : Option α :=
  match xs with
  | [] => none
  | y :: _ => some y

#eval [].head? (α := Int)
-- none
#eval ([] : List Int).head?
-- none
```

### 🟡 `Prod` — pairs `α × β`

A concrete pair is a struct (a tuple is anonymous, a struct is named — the
price of C; **G2** for the generic `×`, `.fst`/`.snd` notation and `×`-nesting):

```c
typedef struct { const char *fst; int snd; } StrInt;
const StrInt fives = {"five", 5};
```

The book's `String × Int × Nat` (nested pairs) would be a struct of a struct:

<!-- clean: continue -->
```c
typedef struct { int fst; unsigned snd; } IntNat;
typedef struct { const char *fst; IntNat snd; } StrIntNat;
const StrIntNat sevens = {"VII", {7, 7}};
```

```lean
structure Prod (α : Type) (β : Type) : Type where
  fst : α
  snd : β

def fives : String × Int := { fst := "five", snd := 5 }
def fives : String × Int := ("five", 5)
def sevens : String × Int × Nat := ("VII", 7, 4 + 3)
def sevens : String × (Int × Nat) := ("VII", (7, 4 + 3))
```

### 🟡 `Sum` — `α ⊕ β` (the pet-name example)

A sum with two *same-sized* cases is a tagged struct:

```c
typedef struct { bool isCat; const char *name; } PetName;   /* inl = dog, inr = cat */

const PetName animals[5] = {
  {false, "Spot"}, {true, "Tiger"}, {false, "Fifi"}, {false, "Rex"}, {true, "Floof"}
};

unsigned howManyDogs(const PetName pets[], unsigned n) {
  unsigned dogs = 0;
  for (unsigned i = 0; i < n; i++)
    if (!pets[i].isCat) dogs++;
  return dogs;
}
```

```lean
inductive Sum (α : Type) (β : Type) : Type where
  | inl : α → Sum α β
  | inr : β → Sum α β

def PetName : Type := String ⊕ String

def animals : List PetName :=
  [Sum.inl "Spot", Sum.inr "Tiger", Sum.inl "Fifi",
   Sum.inl "Rex", Sum.inr "Floof"]

def howManyDogs (pets : List PetName) : Nat :=
  match pets with
  | [] => 0
  | Sum.inl _ :: morePets => howManyDogs morePets + 1
  | Sum.inr _ :: morePets => howManyDogs morePets
```

### `Unit` and `void`

`Unit` is `void` (a `void` function is `: Unit`; `IO Unit` when it prints).

### ❌ Other material of the section (G3, G4, G7)

Polymorphic `inductive ArithExpr (ann : Type)`, the universe-level error
examples (`MyType`), the dependent return type `posOrNegThree`, and the
termination-checker error for `sameLength` are all about Lean's type theory and
have no C counterpart:

```lean
inductive ArithExpr (ann : Type) : Type where
  | int : ann → Int → ArithExpr ann
  | plus : ann → ArithExpr ann → ArithExpr ann → ArithExpr ann
  | minus : ann → ArithExpr ann → ArithExpr ann → ArithExpr ann
  | times : ann → ArithExpr ann → ArithExpr ann → ArithExpr ann

inductive Sign where
  | pos
  | neg

def posOrNegThree (s : Sign) :
    match s with | Sign.pos => Nat | Sign.neg => Int :=
  match s with
  | Sign.pos => (3 : Nat)
  | Sign.neg => (-3 : Int)

inductive MyType : Type where
  | ctor : (α : Type) → α → MyType   -- error: universe level too large
```

`deriving Repr` (printing an enumeration) is applied automatically to every
`enum`/`struct` (main-spec §4.5), so `[Firewood.birch, …]` printing needs no
annotation in C.

---

## 1.7 Characters, Strings, and Slices

| Lean | Clean |
| --- | --- |
| `"Hello, " ++ "world"` | `strcat` statement ✅ |
| `String.length` | `strlen(s)` — counts characters, not bytes ✅ |
| `c.isDigit`, `c.toUpper`, … | `isdigit(c)`, `toupper(c)` ✅ |
| `s.toList[i]!` | `s[i]` (O(n) in Lean, lint W011) ✅ |
| `'a'` / `"text"` literals | same ✅ |
| `String.push s c`, `s.push c` | ❌ (`strcat` takes a string, not a `char`) G8 |
| `drop`, `dropEnd`, `dropWhile`, `dropEndWhile`, `copy` (slices) | ❌ G8 |

```c
unsigned countDigits(const char *s) {
  unsigned n = 0;
  for (unsigned i = 0; i < strlen(s); i++)
    if (isdigit(s[i])) n++;
  return n;
}

const char *helloWorld(void) {            /* #eval "Hello, " ++ "world" */
  char s[] = "Hello, ";
  strcat(s, "world");
  return s;
}
```

### ❌ Slices and the `String` API (G8)

C strings are mutable char arrays; Lean's are immutable values with a rich
slice API (`drop`, `dropEnd`, `dropWhile`, `dropEndWhile`, …) that C spells with
pointer arithmetic. Clean refuses pointer arithmetic (**E101**) and string
writes by index (**E101**), and has no `char` → string conversion, so these are
left undone:

```lean
#eval String.push "Hello" '!'

#eval "Hello".push '!'

#eval (("small tortoiseshell".drop 6).dropEnd 5).copy

#eval "red admiral".dropEndWhile 'l'

#eval "the the butterfly".dropWhile "the "

#eval ("a gray grayling".drop 2).dropWhile "gray "

#eval ("red admiral".dropEndWhile Char.isAlpha).copy
```

---

## 1.8 Additional Conveniences

### Pattern-matching definitions → `switch` / `if`

`def even : Nat → Bool | 0 => true | n + 1 => not (even n)` is the same C
`even` as in 1.5. `halve` with the `n + 2` pattern is a test and a subtraction:

```c
unsigned halve(unsigned n) {
  if (n < 2) return 0;               /* | 0 => 0  | 1 => 0 */
  return halve(n - 2) + 1;           /* | n + 2 => halve n + 1 */
}
```

🟡 **G29.** The `switch` spelling translates but **Lean rejects it**: in
`match n with | 0 | 1 => … | _ => halve (n - 2) + 1` the `_` arm does not know that
`n ≥ 2`, so Lean cannot prove `n - 2 < n` and the recursion does not terminate in
its eyes. With `if (n < 2)` the condition is a hypothesis in the `else` branch
and the proof goes through. (Lean's own `| n + 2 =>` pattern would be the fix;
Clean does not emit it yet.)

<!-- clean: lean-rejects -->
```c
unsigned halveSwitch(unsigned n) {
  switch (n) {
    case 0:
    case 1:  return 0;
    default: return halveSwitch(n - 2) + 1;   /* Lean: fail to show termination */
  }
}
```

Grouped empty `case` labels are or-patterns (`| 0 | 1 =>`).

`drop : Nat → List α → List α` is list-specific (see ❌ below): on an array
it would be an index offset, not recursion.

### Namespaces

Lean's `namespace … end` is the file-wide `#pragma lean namespace` (one
namespace per file, 🟡 G13). Inside the same file the functions call each
other without qualification, so `open NewNamespace in` has nothing to do:

```c
#pragma lean namespace NewNamespace
unsigned triple(unsigned x)    { return 3 * x; }
unsigned quadruple(unsigned x) { return 2 * x + 2 * x; }
unsigned timesTwelve(unsigned x) { return quadruple(triple(x)); }
```

The book's `Nat.double (x : Nat) : Nat := x + x` and call `(4 : Nat).double`
(dot notation resolves through the type's namespace) is a free function in C:

```c
unsigned Nat_double(unsigned x) { return x + x; }
printf("%d", Nat_double(4));      /* 8 */
```

### ❌ Everything else in 1.8 (G1–G5, G14)

Auto-bound implicit arguments (`{α : Type}`), `let rec` helpers over lists,
destructuring `let (xs, ys) := …`, `fun`/`·` lambdas, `if let`, and
constructors with payloads (`Inline.string s`) all rest on the missing
polymorphism, tuples, lambdas and tagged unions. The original code:

```lean
def length : List α → Nat
  | [] => 0
  | y :: ys => Nat.succ (length ys)

def drop : Nat → List α → List α
  | Nat.zero, xs => xs
  | _, [] => []
  | Nat.succ n, x :: xs => drop n xs

def fromOption (default : α) : Option α → α
  | none => default
  | some x => x

def unzip : List (α × β) → List α × List β
  | [] => ([], [])
  | (x, y) :: xys =>
    let (xs, ys) : List α × List β := unzip xys
    (x :: xs, y :: ys)

def reverse (xs : List α) : List α :=
  let rec helper : List α → List α → List α
    | [], soFar => soFar
    | y :: ys, soFar => helper ys (y :: soFar)
  helper xs []

def id (x : α) : α := x

fun x => x + 1
fun {α} x => x
(· , ·) 1 2
(1, ·) 2

def double : Nat → Nat := fun
  | 0 => 0
  | k + 1 => double k + 2

def timesTwelve (x : Nat) :=
  open NewNamespace in
  quadruple (triple x)

inductive Inline : Type where
  | lineBreak
  | string : String → Inline
  | emph : Inline → Inline
  | strong : Inline → Inline

def Inline.string? (inline : Inline) : Option String :=
  match inline with
  | Inline.string s => some s
  | _ => none

def Inline.string? (inline : Inline) : Option String :=
  if let Inline.string s := inline then
    some s
  else none
```

String interpolation `s!"three fives is {5 + 10}"` is `printf("three fives is %d\n", 5 + 10)`
(main-spec §8.1):

```c
printf("three fives is %d", 5 + 10);   /* "three fives is 15" */
```
