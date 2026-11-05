# Clean++ — type classes, overloading, constructors and sum types in C/C++ style

**Status:** design proposal, *not built*. Nothing here is normative until the owner
decides the open questions at the end (they are also queued in
[open-decisions.md](open-decisions.md) as OD12–OD16). main-spec §15 lists
"🔮 C++ subset (templates → polymorphism, overloading → type classes), tagged
unions" for *Later*; this document says what that subset should look like.

**Why.** The book walkthrough in [`manual/fp-in-lean/`](../manual/fp-in-lean/README.md)
showed that four missing capabilities block most of chapters 3–7 of *Functional
Programming in Lean*: type classes (gap G20), sum types with payloads (G3),
function values (G5) and polymorphism (G4), with `Option`/`Except`/tuples (G1, G2)
close behind. C has no spelling for any of them. C++ has a spelling for **every one**,
and a C/C++ programmer already knows it.

**Every Lean block in this document is checked:** the target output is collected
in [cpp-front-end-examples.lean](cpp-front-end-examples.lean), which Lean 4.35.0
accepts (`lean spec/cpp-front-end-examples.lean`).

---

## 1. Principles

| # | Principle | Consequence |
| --- | --- | --- |
| P1 | **Existing C++ syntax only.** Every construct is C++20 (C++23 for `std::expected`) that a C++ parser accepts. Nothing new is invented except attributes in the `[[lean::…]]` namespace that already exists. | IDEs, clang-format and clangd keep working on Clean++ files; the code need not *compile* (D2), only parse. |
| P2 | **C++ spelling, Lean semantics** (D4 extended). Value semantics everywhere: an object is a Lean value, `const T&` is "by value", a mutating method returns the new object. | No aliasing, no heap, no lifetimes to model. |
| P3 | **Nominal, explicit instances.** A Lean instance comes from something the user *wrote* — an operator definition, a template specialisation, a conversion — never from structural matching (C++ concepts are structural; Lean classes are not). | Instance search is predictable; refusals point at the missing definition. |
| P4 | **One C++ form ↔ one Lean form**, chosen by the translator from the declarations (like D19 picks conversions). | Reviewable output (D3); no guessing (D5). |
| P5 | **Resolve statically what C++ resolves statically** (function overloading, `Base::f` calls), and leave to Lean what C++ leaves to templates (instance search at instantiation). | Overload resolution happens in clean; instance resolution in Lean. |
| P6 | **C first.** Where plain C has an idiom (the tagged union), it is accepted as well, so a `.c` file can use the feature without becoming C++. | Sum types are reachable from C (§5.1). |

### 1.1 The dialect switch

A file is Clean++ when it is named `*.cpp`/`*.cc`/`*.hpp` or contains
`#pragma lean cpp`. It includes `lean++.h` (which includes `lean.h`) — a parse-only
header declaring the names below (`std::optional`, `Box`, the Lean class names used
in `requires`). Everything in the C subset keeps its meaning; additionally
`std::string` is `String`, `bool`/`true`/`false` are keywords, and `struct`/`class`
are the same thing (no access control; `public:`/`private:` are accepted and
ignored, `private` on a member function → Lean `private def`).

---

## 2. Summary

| Lean (book) | Clean++ spelling | § | Gap closed |
| --- | --- | --- | --- |
| `structure` + `def S.f (self : S)` | `struct S { …; R f(…) const { … } };` | 3.1 | G10 |
| mutating method | non-`const` member function: returns the updated `self` | 3.1 | — |
| custom constructor `S.mk'` / `OfNat` | constructor `S(unsigned n) : f(…) {}` | 3.2 | G10, G20 |
| default field value `x : T := v` | default member initializer `T x = v;` | 3.3 | G23 |
| `structure B extends A` | `struct B : A { … };` (`virtual A` for shared diamonds) | 3.4 | G22 |
| generic `{α : Type}` | `template <typename T>` | 4.1 | G4 |
| instance constraints `[Add α]` | `requires Add<T>` | 4.2 | G4, G20 |
| `class C (α : Type) where …` | trait template `template <typename T> struct C { static … };` | 6.1 | G20 |
| `instance : C Nat where …` | explicit specialisation `template <> struct C<unsigned> { … };` | 6.2 | G20 |
| `instance [C α] : C (F α)` | partial specialisation with `requires` | 6.3 | G20 |
| `outParam` | associated type `using Out = …;` in the trait | 6.4 | G20 |
| `Add`, `Mul`, `BEq`, `Ord`, `LT`, `GetElem`, `CoeFun`, `Coe`, `OfNat`, `ToString`, `Hashable` instances | `operator+`, `operator*`, `operator==`, `operator<=>`, `operator<`, `operator[]`, `operator()`, `operator T()`, converting constructor, `to_string`, `std::hash<T>` | 7 | G20 |
| ad hoc overloading (`Pos.plus` vs `Nat.plus`) | C++ function overloading, resolved by clean | 8 | G20 |
| `fun x => e`, `α → β` | lambda `[](int x) { return e; }`, `std::function<B(A)>`, function pointer | 9 | G5 |
| `inductive` with payloads | C tagged union, or `std::variant` | 5 | G3 |
| recursion through the type | `Box<T>` / `const T *` field | 5.2 | G3 |
| `Option α` | `std::optional<T>` | 5.3 | G1 |
| `Except ε α` | `std::expected<T, E>` | 5.3 | G1 |
| `α × β`, `let (a, b) := …` | `std::pair`, `std::tuple`, structured bindings `auto [a, b] = …;` | 5.4 | G2, G14 |
| `Functor`, `Monad` over `F : Type → Type` | template template parameter; `co_await` for `←` | 10 | G21 (🔮) |

Not proposed (refused with a diagnostic, as today): `virtual` member functions
(dynamic dispatch), `new`/`delete`, references or pointers used for *mutation*,
exceptions (`throw` — use `std::expected`), `static` mutable members, `goto`,
template metaprogramming beyond the forms below.

---

## 3. Structures: methods, constructors, defaults, inheritance

### 3.1 Member functions → `S.f` with dot notation

A member function is a function in the struct's namespace whose first explicit
argument is `self`; `p.f(a)` is Lean's dot notation `p.f a`, which reads the same.
`this->x` and bare `x` both mean `self.x`.

* a **`const`** member function is pure: `def S.f (self : S) … : R`;
* a **non-`const`** `void` member function changes the object: it is emitted as
  returning the updated `S` (exactly like a D20 array writer), and the call
  statement `p.scale(2.0);` becomes `p := p.scale 2.0`;
* a non-`const` non-`void` member function is refused (it would need a pair, G2) —
  until tuples land, write a `const` function plus a mutator.

```cpp
struct Point {
  double x, y;
  Point modifyBoth(std::function<double(double)> f) const { return {f(x), f(y)}; }
  void scale(double k) { x *= k; y *= k; }
};

Point demo() {
  Point p = {4.3, 3.4};
  p.scale(2.0);
  p = p.modifyBoth(floor);
  p = p.modifyBoth([](double v) { return v + 1.0; });
  return p;
}
```

```lean
structure Point where
  x : Float
  y : Float
  deriving Repr, BEq, Inhabited

def Point.modifyBoth (self : Point) (f : Float → Float) : Point := { x := f self.x, y := f self.y }

def Point.scale (self : Point) (k : Float) : Point := Id.run do
  let mut self := self
  self := { self with x := self.x * k }
  self := { self with y := self.y * k }
  return self

def demo : Point := Id.run do
  let mut p : Point := ⟨4.3, 3.4⟩
  p := p.scale 2.0
  p := p.modifyBoth Float.floor
  p := p.modifyBoth (fun v => v + 1.0)
  return p
```

`static` member functions are `def S.f` without `self` (called `S::f(…)` →
`S.f …`). The book's `Point.modifyBoth`, `Pos.toNat`, `NonEmptyList.get?`,
`Config.inDirectory` … all become ordinary member functions.

### 3.2 Constructors

A struct with no user constructor is an aggregate: brace initialisation is Lean's
anonymous constructor `⟨…⟩` / `{ x := … }`, as today. A **user constructor** is a
function `S.new` (the structure's own `S.mk` stays the all-fields constructor):

```cpp
struct Pos {
  unsigned pred;
  Pos(unsigned n) : pred(n - 1) {}          // converting constructor (not `explicit`)
  unsigned toNat() const { return pred + 1; }
};
```

```lean
structure Pos where
  pred : Nat
  deriving Repr, BEq, Inhabited

def Pos.new (n : Nat) : Pos := { pred := n - 1 }
def Pos.toNat (self : Pos) : Nat := self.pred + 1
instance {n : Nat} : OfNat Pos n := ⟨Pos.new n⟩
```

* `Pos p(7);`, `Pos p{7};`, `Pos(7)` in an expression → `Pos.new 7`.
* A **non-`explicit` constructor from one integer** is C++'s implicit conversion
  from a number, so it also gives `OfNat Pos n`: `Pos seven = 7;` → `def seven : Pos := 7`.
  This is the book's `instance : OfNat Pos (n + 1)` (§3.1), except that C++ cannot
  restrict the literal to `n ≥ 1` — `Pos(0)` is `pred := 0 - 1 = 0` in Lean
  (OD14 asks whether to emit `OfNat Pos (n + 1)` instead when the constructor
  body is `n - 1`).
* A non-`explicit` constructor from one value of another type is `Coe A S`;
  `explicit` constructors give neither instance.
* Member-initializer lists and constructor bodies are both accepted; the body runs
  after the initialisers, as in C++. A constructor body may not call methods on
  `this` before every field is set (refused: Lean has no partially built value).
* `[[lean::ctor("point")]] struct Point { … };` renames Lean's `mk` (the book's
  `structure Point where point ::`).

### 3.3 Default member initializers → field defaults

```cpp
struct Config {
  bool useASCII = false;
  std::string currentPrefix = "";
};

Config c1 = {};                       // both defaults
Config c2 = {.useASCII = true};       // the other one defaults
```

```lean
structure Config where
  useASCII : Bool := false
  currentPrefix : String := ""
  deriving Repr

def c1 : Config := {}
def c2 : Config := { useASCII := true }
```

The default may mention earlier fields (`bool large = size == Size::large;`),
exactly as Lean allows. In the C subset, a field without a default keeps today's
rule: a missing initialiser is that type's zero. (This one is cheap enough to
also accept in `.c` files as an extension, OD15.)

### 3.4 Inheritance → `extends`

```cpp
struct MythicalCreature {
  bool large;
  bool small() const { return !large; }
};

struct Monster : MythicalCreature { std::string vulnerability; };

Monster troll = {{true}, "sunlight"};           // C++17 aggregate with a base
bool b = troll.small();                         // inherited member function
MythicalCreature c = troll;                     // slicing = the parent projection

struct Helper : virtual MythicalCreature { std::string assistance, payment; };
struct MonstrousAssistant : Monster, Helper {}; // shared base: `virtual` in both parents
```

```lean
structure MythicalCreature where
  large : Bool
  deriving Repr, BEq, Inhabited

def MythicalCreature.small (self : MythicalCreature) : Bool := !self.large

structure Monster extends MythicalCreature where
  vulnerability : String
  deriving Repr, BEq, Inhabited

def troll : Monster := { large := true, vulnerability := "sunlight" }

#eval troll.small                      -- Lean finds `MythicalCreature.small` through `extends`
#eval troll.toMythicalCreature         -- the slice

structure MonstrousAssistant extends Monster, Helper where
  deriving Repr
```

* `struct B : A` (public, non-virtual base) → `extends A`. Several bases →
  `extends A, B`; Lean merges fields with the same name and origin, which is C++'s
  **virtual** inheritance. So a diamond whose shared base is not `virtual` in every
  path is **refused** (C++ would keep two copies; Lean cannot).
* Slicing `A a = b;`, passing a `B` where an `A` is expected, and
  `static_cast<const A&>(b)` → `b.toA`.
* An inherited member function is called with plain dot notation; Lean resolves
  it through the parent structure (checked above).
* `virtual` *functions* are refused: there is no dynamic dispatch on Lean
  structures. Sum types (§5) are the replacement.

---

## 4. Templates → polymorphism

### 4.1 Type parameters

`template <typename T>` before a struct or function is Lean's implicit type
argument. A template struct is a structure with a parameter:

```cpp
template <typename T> struct PPoint { T x; T y; };

template <typename T>
PPoint<T> replaceX(PPoint<T> point, T newX) { point.x = newX; return point; }
```

```lean
structure PPoint (T : Type) where
  x : T
  y : T
  deriving Repr, BEq, Inhabited

def replaceX {T : Type} (point : PPoint T) (newX : T) : PPoint T :=
  { point with x := newX }
```

Explicit template arguments `replaceX<unsigned>(p, 5)` → `replaceX (T := Nat) p 5`
(the book's `replaceX Nat natOrigin 5`). Non-type template parameters
`template <unsigned N>` are a later step (they are `Vector α n` / `BitVec N`, OD-free
because `bv(N)` already exists).

### 4.2 Constraints → instance arguments

A `requires` clause names Lean classes (declared in `lean++.h` so the C++ parses):
`requires Add<T> && Zero<T>` → `[Add T] [Zero T]`. Clean **adds** the constraints
the body needs and reports the ones the user forgot (a body that uses `a + b`
without `Add<T>` is refused with the fix "add `requires Add<T>`" — OD4's option
(b), explicit constraints, with the inference used only for the diagnostic):

| use in a template body | needed constraint |
| --- | --- |
| `a + b`, `a - b`, `a * b`, `a / b` | `Add<T>`, `Sub<T>`, `Mul<T>`, `Div<T>` |
| a literal `0` / `1` / `n` of type `T` | `OfNat<T, 0>` … (or `Zero<T>`) |
| `a == b` | `BEq<T>` |
| `a < b`, `a <= b` | `LT<T>` + decidability, or `Ord<T>` |
| `xs[i]` on an array of `T` | `Inhabited<T>` (for the panic value of `xs[i]!`) |
| `to_string(a)`, `printf("%s", …)` | `ToString<T>` |

```cpp
template <typename T> requires Add<T> && OfNat<T, 0> && Inhabited<T>
T sumOfContents(const T xs[], unsigned n) {
  T s = 0;
  for (unsigned i = 0; i < n; i++) s += xs[i];
  return s;
}
```

```lean
def sumOfContents {T : Type} [Add T] [OfNat T 0] [Inhabited T] (xs : Array T) (n : Nat) : T := Id.run do
  let mut s : T := 0
  for i in [0:n] do
    s := s + xs[i]!
  return s
```

(The book's `List.sumOfContents [Add α] [OfNat α 0]` — the extra `Inhabited` is the
price of array indexing with a panic.)

---

## 5. Sum types (`inductive` with payloads)

### 5.1 The C tagged union (accepted in `.c` and `.cpp`)

The idiom every C programmer writes: a struct whose first member is an `enum` tag
and whose second is an anonymous `union` with **one member per enumerator, in the
same order**. Each union member is a struct (its fields are the constructor's
arguments) or absent for a payload-free constructor.

```c
enum Arith { plus, minus, times, divide };

struct Expr {
  enum { CONST, PRIM } tag;
  union {
    struct { int value; } constant;                               /* CONST */
    struct { enum Arith op; Box(struct Expr) lhs, rhs; } prim;   /* PRIM  */
  };
};
```

```lean
inductive Expr where
  | CONST (value : Int)
  | PRIM (op : Arith) (lhs : Expr) (rhs : Expr)
  deriving Repr, BEq, Inhabited
open Expr
```

* **Construction:** a designated initialiser that sets the tag and its member —
  `(struct Expr){.tag = CONST, .constant = {14}}` → `CONST 14`. Setting a member
  that does not belong to the tag is refused. In Clean++ the shorthand
  `Expr::CONST(14)` is accepted too.
* **Pattern matching:** `switch (e.tag)` with one `case` per enumerator; inside
  `case PRIM:` the member `e.prim.lhs` is the variable bound by the pattern
  `| PRIM op lhs rhs =>`. Reading `e.prim` outside a `case PRIM:` is refused (it
  is the classic tagged-union bug, and Lean has no way to express it).
* `if (e.tag == CONST)` without a `switch` is a one-armed `match` with `| _ =>`.
* Recursion through the type (`lhs` is itself an `Expr`) needs indirection in C:
  see 5.2.

The alternative C++ spelling is `std::variant`:

```cpp
struct Const { int value; };
struct Prim  { Arith op; Box<struct Expr> lhs, rhs; };
struct Expr : std::variant<Const, Prim> {};      // each alternative = one constructor
```

with `std::visit` / `std::holds_alternative` / `std::get_if` mapped to `match`.
It reads worse for C programmers and needs more machinery; OD13 asks which one (or
both) to build.

### 5.2 Recursive fields: `Box<T>`

A field of the type being defined needs a pointer in C. Because Lean values are
immutable and never null, Clean accepts exactly one form, declared in `lean.h`:

```c
#define Box(T) const T *        /* a non-null, never-written pointer = a Lean field of type T */
```

`Box(struct Expr) lhs` is a field `lhs : Expr`; `*e.prim.lhs` and `e.prim.lhs->tag`
read it; taking `&x` of a local to build a node is accepted only in a constructor
expression. `NULL` in a `Box` is refused — an optional child is
`std::optional<Box<T>>` (5.3), i.e. `Option T`. This closes `BinTree`, `Expr`,
`JSON`, `List`-like types in the book.

### 5.3 `std::optional` and `std::expected` → `Option`, `Except`

```cpp
std::optional<int> applyPrim(Arith op, int x, int y) {
  switch (op) {
    case plus:  return x + y;
    case minus: return x - y;
    case times: return x * y;
    case divide:
      if (y == 0) return std::nullopt;
      return x / y;
  }
}

std::optional<int> evaluate(const Expr &e) {
  switch (e.tag) {
    case CONST: return e.constant.value;
    case PRIM: {
      std::optional<int> v1 = evaluate(*e.prim.lhs);
      if (!v1) return std::nullopt;
      std::optional<int> v2 = evaluate(*e.prim.rhs);
      if (!v2) return std::nullopt;
      return applyPrim(e.prim.op, *v1, *v2);
    }
  }
}
```

```lean
def applyPrim (op : Arith) (x y : Int) : Option Int := Id.run do
  match op with
  | plus => return some (x + y)
  | minus => return some (x - y)
  | times => return some (x * y)
  | divide =>
    if y = 0 then
      return none
    return some (x / y)

def evaluate (e : Expr) : Option Int := Id.run do
  match e with
  | CONST value =>
    return some value
  | PRIM op lhs rhs =>
    let some v1 := evaluate lhs | return none
    let some v2 := evaluate rhs | return none
    return applyPrim op v1 v2
```

Rules: `return x;` in a function returning `std::optional<T>` is `return some x`;
`std::nullopt` is `none`; the guard pattern
`std::optional<T> v = e; if (!v) return std::nullopt;` (and `if (!v.has_value())`)
becomes Lean's **let-else** `let some v := e | return none`, after which `*v` /
`v.value()` is `v`. `v.value_or(d)` is `v.getD d`. Other uses of `*v` where the
translator cannot see the test are `v.get!` (a panic, lint like W010).

`std::expected<T, E>` is the same with `Except E T`: `return std::unexpected(e);`
→ `return .error e`, a plain `return x;` → `return .ok x`, and the guard
`if (!r) return std::unexpected(r.error());` → `let .ok v := r | return r`
(checked in the examples file with `applyPrimE`/`evaluateE`). This is the book's
chapter 4 example (`evaluateOption`, `evaluateExcept`) written the way a C++23
programmer would write it.

### 5.4 Pairs and tuples

`std::pair<A, B>` and `std::tuple<A, B, C>` → `A × B`, `A × B × C`;
`{a, b}` / `std::make_pair(a, b)` → `(a, b)`; `p.first`/`p.second`/`std::get<0>(t)`
→ `.1`/`.2`/…; a structured binding `auto [q, r] = divmod(17, 5);` →
`let (q, r) := divmod 17 5`. Non-`void` functions that write an array parameter
(today refused) can then return `std::pair<R, Array>` instead.

---

## 6. Type classes as trait templates

C++ has two "type class" mechanisms: concepts (structural — a type satisfies a
concept if the expressions compile) and **trait templates** with explicit
specialisation (nominal — `std::hash<T>`, `std::formatter<T>`, `std::numeric_limits<T>`
only exist for the types someone specialised them for). Lean classes are nominal,
so traits are the match (P3).

### 6.1 Declaring a class

A primary class template **with declarations but no definitions** of `static`
member functions is a Lean `class`. A member function *with* a body is a default
method.

```cpp
template <typename T>
struct Plus {
  static T plus(T a, T b);            // a method: no body
};
```

```lean
class Plus (T : Type) where
  plus : T → T → T
```

A trait that inherits from another trait is a class that `extends` it (Lean's
superclass): `template <typename T> struct Monoid : Plus<T> { static T zero(); };`.

### 6.2 Instances: explicit specialisation

```cpp
template <> struct Plus<unsigned> { static unsigned plus(unsigned a, unsigned b) { return a + b; } };
template <> struct Plus<Pos>      { static Pos plus(Pos a, Pos b) { return a + b; } };

template <typename T> requires Plus<T>
T twice(T x) { return Plus<T>::plus(x, x); }
```

```lean
instance : Plus Nat where
  plus a b := a + b
instance : Plus Pos where
  plus a b := a + b

def twice {T : Type} [Plus T] (x : T) : T := Plus.plus x x
```

`Plus<T>::plus(x, x)` → `Plus.plus x x`. A specialisation that misses a method
without a default is refused (Lean would reject the instance). The book's chapter 3
`Plus`/`instance : Plus Nat`/`Plus Pos`/`#eval plus 5.2 917.25861` (no `Float`
instance) translate one to one, including the error.

### 6.3 Instances with constraints: partial specialisation

```cpp
template <typename T> requires Hashable<T>
struct Hashable<NonEmptyList<T>> {
  static uint64_t hash(NonEmptyList<T> xs) { return mixHash(hash(xs.head), hash(xs.tail)); }
};
```

→ `instance {T : Type} [Hashable T] : Hashable (NonEmptyList T) where hash xs := …`.

### 6.4 Associated types → `outParam`

A member type alias in the trait is an output parameter: the instance determines
it, exactly the book's `HPlus (α β : Type) (γ : outParam Type)`.

```cpp
template <typename A, typename B> struct HPlus {
  using Out = void;                       // a placeholder in the primary: an outParam
  static Out hPlus(A a, B b);
};
template <> struct HPlus<unsigned, Pos> {
  using Out = Pos;
  static Pos hPlus(unsigned n, Pos p) { return Pos(n + p.toNat()); }
};
```

```lean
class HPlus (A : Type) (B : Type) (Out : outParam Type) where
  hPlus : A → B → Out
instance : HPlus Nat Pos Pos where
  hPlus n p := Pos.new (n + p.toNat)
```

(The placeholder `void` only makes the primary template valid C++; every
specialisation must give `Out` a real type. OD16 asks whether to mark it with an
attribute instead.) `[[lean::default_instance]]` on a
specialisation is `@[default_instance]`.

---

## 7. Operator overloading → Lean's standard classes

An operator defined for a user type is an instance of the class Lean uses for that
operator. Defined as a free function or as a member (`a.operator+(b)`), same result.

| C++ | Lean instance | Notes |
| --- | --- | --- |
| `S operator+(S, S)` (`-`, `*`, `/`, `%`) | `Add S` (`Sub`, `Mul`, `Div`, `Mod`) | heterogeneous `S operator+(A, B)` → `HAdd A B S` |
| `S operator-(S)` | `Neg S` | |
| `bool operator==(S, S)` | `BEq S` | `= default` → `deriving BEq` (already automatic) |
| `std::strong_ordering operator<=>(S, S)` | `Ord S` (+ `LT`/`LE`/`Max`/`Min` via `ltOfOrd`, `leOfOrd`, `maxOfLe`) | `std::strong_ordering::less/equal/greater` → `.lt/.eq/.gt` |
| `bool operator<(S, S)` alone | `LT S` with a `Bool`-valued `lt` (+ decidability) | |
| `T operator[](unsigned i) const` (member) | `GetElem S Nat T (fun _ _ => True)` | `xs[i]` needs no proof; a body that may fail uses `!` inside |
| `R operator()(A) const` (member) | `CoeFun S (fun _ => A → R)` | the book's `Adder`: `add5(3)` → `add5 3` |
| `operator T() const` (non-`explicit`) | `Coe S T` | the book's `Coe Pos Nat` |
| non-`explicit` constructor `S(unsigned)` | `OfNat S n` (+ `Coe Nat S`) | §3.2 |
| `std::string to_string(S)` | `ToString S` | also what `printf("%s", …)` and `s!"{…}"` use |
| `std::ostream& operator<<(std::ostream&, S)` | `ToString S` | body restricted to `return os << e1 << e2 …;` |
| `template <> struct std::hash<S> { size_t operator()(S) const; };` | `Hashable S` | `size_t` here is `UInt64` |
| `S operator++` etc., compound `+=` | — | `x += y` already means `x := x + y`; `++` on a user type is refused |

The chapter 3 `Pos` in full:

```cpp
Pos operator+(Pos a, Pos b) { return Pos(a.toNat() + b.toNat()); }
Pos operator*(Pos a, Pos b) { return Pos(a.toNat() * b.toNat()); }
std::string to_string(Pos p) { return std::to_string(p.toNat()); }
std::strong_ordering operator<=>(Pos a, Pos b) {
  if (a.pred < b.pred) return std::strong_ordering::less;
  if (a.pred > b.pred) return std::strong_ordering::greater;
  return std::strong_ordering::equal;
}
// and, inside `struct Pos`:  operator unsigned() const { return toNat(); }

const Pos seven = 7;
const Pos fourteen = seven + seven;
printf("There are %s\n", to_string(fourteen).c_str());
```

```lean
def Pos.add (a b : Pos) : Pos := Pos.new (a.toNat + b.toNat)
instance : Add Pos := ⟨Pos.add⟩
def Pos.mul (a b : Pos) : Pos := Pos.new (a.toNat * b.toNat)
instance : Mul Pos := ⟨Pos.mul⟩
instance : ToString Pos := ⟨fun p => toString p.toNat⟩

def Pos.compare (a b : Pos) : Ordering := Id.run do
  if a.pred < b.pred then
    return .lt
  if a.pred > b.pred then
    return .gt
  return .eq
instance : Ord Pos := ⟨Pos.compare⟩
instance : LT Pos := ltOfOrd
instance : LE Pos := leOfOrd
instance : Max Pos := maxOfLe

instance : Coe Pos Nat := ⟨Pos.toNat⟩

def seven : Pos := 7
def fourteen : Pos := seven + seven
#eval s!"There are {fourteen}"            -- "There are 14"
#eval [1, 2, 3, 4].drop (2 : Pos)          -- [3, 4], through `Coe Pos Nat`
```

`operator[]` for the book's `NonEmptyList`:

```cpp
template <typename T> requires Inhabited<T>
struct NonEmptyList {
  T head;
  std::vector<T> tail;
  T operator[](unsigned i) const { return i == 0 ? head : tail[i - 1]; }
};
```

```lean
structure NonEmptyList (T : Type) where
  head : T
  tail : Array T
  deriving Repr

def NonEmptyList.get {T : Type} [Inhabited T] (self : NonEmptyList T) (i : Nat) : T :=
  if i = 0 then self.head else self.tail[i - 1]!

instance {T : Type} [Inhabited T] : GetElem (NonEmptyList T) Nat T (fun _ _ => True) where
  getElem xs i _ := xs.get i
```

The book's proof-carrying `GetElem … NonEmptyList.inBounds` (indexing that cannot
fail) is not expressible: a C++ `operator[]` has no precondition in its type. A
`[[lean::bounds("i <= self.tail.size()")]]` attribute could become the validity
predicate later (G18 stays a non-goal under D11).

---

## 8. Function overloading

Two C++ functions with the same name and different parameter types are resolved
**by clean** (P5), using C++'s rules restricted to exact matches plus the D19
conversions. They are emitted in the namespace of their first parameter's type,
so that Lean's dot notation also works:

```cpp
Pos      plus(Pos a, Pos b);             // → def Pos.plus (a b : Pos) : Pos
unsigned plus(unsigned a, unsigned b);   // → def plus (a b : Nat) : Nat   (built-in types are
                                         //   never extended: this one stays at top level)
```

* first parameter of a user type `S` → `def S.f`; a call `f(a, b)` → `S.f a b`
  (printed as `a.f b` when `a` is a variable);
* otherwise one overload may stay unqualified and the others are refused with the
  fix `[[lean::name("…")]]` (the attribute main-spec §3.1 already reserves);
* overloads that differ only in a `const` or reference qualifier are one function.

This is deliberately *not* Lean's overloaded-identifier elaboration (two opened
namespaces with the same name): that is order-sensitive and its errors are hard to
read. (OD16 records the choice.)

---

## 9. Function values

| C++ | Lean |
| --- | --- |
| `[](int x) { return x + 1; }` | `fun x => x + 1` |
| `[k](int x) { return x + k; }` (capture by value) | `fun x => x + k` |
| capture by reference `[&]` | refused unless nothing captured is written (then by value) |
| parameter `std::function<double(double)> f`, `double (*f)(double)`, `auto f`, template `F f` | `(f : Float → Float)` |
| passing a function name `floor`, `toNat` | the Lean function (`Float.floor`, `Pos.toNat`) |
| a lambda stored in a local `auto g = …;` | `let g := fun …` |
| a lambda that mutates a captured variable | refused (it would be a closure over a `let mut`, which Lean forbids — the book's chapter 6 "cannot be mutated" message) |

This closes the book's `Point.modifyBoth`, `Array.map`, `List.filter`, `twice`,
`nTimes` with `IO Unit` arguments (a lambda that prints is an `IO` value:
`[]{ puts("shy"); }` → `IO.println "shy"`).

---

## 10. Higher-kinded classes and `do` for user monads (🔮)

Recorded for completeness; it is the last tier.

* A **template template parameter** is a type constructor:
  `template <template <typename> class F> struct Functor { template <typename A, typename B> static F<B> map(std::function<B(A)>, F<A>); };`
  → `class Functor (F : Type → Type) where map : (A → B) → F A → F B` (in Lean,
  the standard `Functor`); `template <> struct Functor<PPoint> { … };` → `instance : Functor PPoint`.
* **`co_await` is `←`.** A function returning `M<T>` where `M` has a `Monad`
  instance is a `do` block in `M`; `co_await e` is `(← e)` and `co_return x` is
  `return x`. This makes the book's `firstThird` read naturally:

```cpp
std::optional<std::pair<int, int>> firstThird(const std::vector<int> &xs) {
  int first = co_await at(xs, 0);         // at(xs, i): std::optional<int>, xs[i]?
  int third = co_await at(xs, 2);
  co_return {first, third};
}
```

```lean
def firstThird (xs : Array Int) : Option (Int × Int) := do
  let first ← xs[0]?
  let third ← xs[2]?
  return (first, third)
```

  (`co_await` on `std::optional` does not compile in standard C++ — it needs a
  library adaptor — but it parses, which is all D2 requires.) Monad transformers
  (`ReaderT`, `StateT`) would be template structs with this tier's machinery; no
  separate syntax is proposed.

---

## 11. What it buys — the book, again

| Book | Before (manual status) | With this design |
| --- | --- | --- |
| ch. 1.4 `Point.modifyBoth`, 1.6 `PPoint α`, `Option`, `Prod`, `Sum` | ❌ | ✅ §3.1, §4, §5.3, §5.4, §5.1 |
| ch. 3 `Pos`, `Plus`, `OfNat`, `ToString`, `HPlus`, `GetElem`, `Coe`, `Hashable`, `Ord` | 🟡/❌ | ✅ §6, §7 — except proof-carrying `GetElem` and `CoeDep`/`CoeSort` |
| ch. 4 `Expr`, `evaluateOption`, `evaluateExcept`, `BinTree` | ❌ | ✅ §5 — `evaluateM [Monad m]` needs §10 |
| ch. 5 `extends`, defaults, `Validate` | 🟡 | ✅ §3.3, §3.4; `Applicative` needs §10 |
| ch. 6 programs | 🟡 | unchanged C; `ReaderT`/`StateT` need §10 |
| ch. 7 dependent types, ch. 8 / interludes' proofs | ❌ | ❌ (out of scope: D11) |

---

## 12. Implementation plan (suggested slices)

Each slice is independent and testable the usual way (golden `.lean`, Lean
type-check, run against the compiled C++ where it compiles).

1. **Structs with methods, constructors, default member initialisers, single
   inheritance** (§3). Parser: member function bodies, ctor-initialiser lists, base
   clauses. No templates yet. Also accept default member initialisers in `.c`
   (OD15).
2. **Operators and `to_string`** (§7) on non-template types.
3. **Tagged unions + `Box` + `std::optional`/`std::expected`/`std::pair`** (§5).
   The one slice that can also land for plain C first (tagged unions, `Box`).
4. **Lambdas and function-typed parameters** (§9) — main-spec §11.2 already lists
   them for v0.2.
5. **Templates and `requires`** (§4) — supersedes the `GENERIC(T)` proposal (OD4).
6. **Trait templates** (§6) and function overloading (§8).
7. 🔮 Template template parameters and `co_await` (§10).

The C++ parser can be grown from the current recursive-descent parser: the subset
has no ADL, no SFINAE, no overloaded templates, no user-defined conversions beyond
§7's, so a full C++ front end (clang) is not needed. OD12 records the alternative
of using libclang.

## 13. Open questions (queued as OD12–OD16)

| ID | Question | Options |
| --- | --- | --- |
| OD12 | **How to parse C++.** | (a) extend clean's own parser to the subset above (no dependency; the subset is small); (b) libclang (complete C++ syntax, a large dependency, still needs our own subset checks). |
| OD13 | **Sum-type spelling.** | (a) C tagged union only (§5.1, works in `.c`); (b) `std::variant` only; (c) both. |
| OD14 | **Converting constructor → `OfNat`.** | (a) `OfNat S n` for every `n` (C++ semantics: the body decides); (b) `OfNat S (n + 1)` when the constructor's body subtracts one, as the book does; (c) no `OfNat` unless `[[lean::literal]]`. |
| OD15 | **Default member initialisers in plain C.** | (a) accept `T x = v;` in a `.c` struct as an extension (not valid C, but GCC/Clang report it clearly); (b) C++ files only. |
| OD16 | **Overloads and associated types.** | Overloads: (a) clean resolves and emits `S.f` (§8); (b) Lean's overloaded identifiers. Associated type in a primary trait: (a) `using Out = void;` placeholder; (b) `[[lean::out]] using Out = void;`. |
