# 5. Functors, Applicative Functors, and Monads — in Clean

Book chapter: <https://lean-lang.org/functional_programming_in_lean/Functors___-Applicative-Functors___-and-Monads/>
Legend and gap IDs: see [README.md](README.md).
C/C++-style syntax for what this chapter misses is designed (not built) in
[spec/cpp-front-end-design.md](../../spec/cpp-front-end-design.md): §3.3 (default member initialisers), §3.4 (`struct B : A` → `extends`, with `virtual` bases for shared parents) and §10.

Almost the whole chapter is *abstraction over type constructors* (`Functor`,
`Applicative`, `Alternative`, universe levels). Clean has none of that (**G20**,
**G21**, **G7**). The parts with a C reading are the **structure inheritance**
section (embedding) and the **validation** example, whose *behaviour* — report
every error instead of the first — is exactly the C loop-and-accumulate idiom.

| Section | Status |
| --- | --- |
| 5.1 Structures and Inheritance | 🟡 embedding ✅; shared parent in diamonds, default fields, `#print` ❌ G22, G23 |
| 5.2 Applicative Functors | 🟡 the validator's behaviour ✅ via an error accumulator; `Applicative`, `Subtype`, `<*>` ❌ G18, G20, G21 |
| 5.3 The Applicative Contract | ❌ equational laws — no C counterpart |
| 5.4 Alternatives | 🟡 try-in-order checks ✅; `Alternative`, `Many`, `<\|>` ❌ G3, G20, G21 |
| 5.5 Universes | ❌ G7 — type theory only |
| 5.6 The Complete Definitions | ❌ G20, G7 |

---

## 5.1 Structures and Inheritance

`structure Monster extends MythicalCreature` is a struct that *contains* a
`MythicalCreature`; C programmers know this as "inheritance by embedding".
Clean supports nested structs, nested initialisers (positional and designated),
and nested field updates:

```c
typedef struct { bool large; } MythicalCreature;

typedef struct {
  MythicalCreature creature;      /* Monster.toMythicalCreature */
  const char *vulnerability;
} Monster;

const Monster troll = {.creature = {.large = true}, .vulnerability = "sunlight"};
```

`def troll : Monster := ⟨⟨true⟩, "sunlight"⟩` is the positional form
`{{true}, "sunlight"}`; `{large := true, vulnerability := "sunlight"}` (the
*flattened* initialiser, where the parent's fields are written directly) cannot be
spelled — C needs the inner braces (🟡 **G22**).

Methods of the parent are *not* inherited by dot syntax (`troll.small`): call the
function on the embedded parent:

<!-- clean: continue -->
```c
bool small(MythicalCreature c) { return !c.large; }

bool trollIsSmall(void) { return small(troll.creature); }   /* troll.toMythicalCreature.small */
```

A second child, and the book's `MonstrousAssistant` that inherits from *both*
(`extends Monster, Helper`):

<!-- clean: continue -->
```c
typedef struct {
  MythicalCreature creature;
  const char *assistance;
  const char *payment;
} Helper;

const Helper nisse = {{false}, "household tasks", "porridge"};

typedef struct {
  Monster monster;                 /* toMonster */
  const char *assistance;          /* Helper's own fields, copied: C cannot say "extends Helper" */
  const char *payment;
} MonstrousAssistant;

const MonstrousAssistant domesticatedTroll = {{{true}, "sunlight"}, "heavy labor", "toy goats"};
```

🟡 **G22.** Lean's `MonstrousAssistant` has *one* `large` field, shared by both
parents (the book shows this with `#print` and `toHelper`, "the overlapping field
is not duplicated"). With plain embedding both would carry their own copy of
`MythicalCreature`, so Helper's fields are copied by hand above and `toHelper` is
not available.

### Default values in a parent — ❌ (G23)

```lean
inductive Size where
  | small
  | medium
  | large
deriving BEq

structure SizedCreature extends MythicalCreature where
  size : Size
  large := size == Size.large

def nonsenseCreature : SizedCreature where
  large := false
  size := .large

abbrev SizesMatch (sc : SizedCreature) : Prop :=
  sc.large = (sc.size == Size.large)

def huldre : SizedCreature where
  size := .medium

example : SizesMatch huldre := by decide
```

C has no per-field defaults, so `huldre` must write `large` itself, and
`SizesMatch` is a `Bool` function rather than a `Prop`:

<!-- clean: continue -->
```c
enum Size { Small, Medium, Large };

typedef struct {
  MythicalCreature creature;
  enum Size size;
} SizedCreature;

bool sizesMatch(SizedCreature sc) {
  bool big = sc.size == Large;
  return sc.creature.large == big;
}

const SizedCreature huldre = {{false}, Medium};
```

(The comparison is split because Clean refuses to compare a `bool` with the
*result of a comparison* in one expression — it would be a C `int`, see
main-spec §5.2.)

### ❌ Reflection output

`#print MonstrousAssistant`, `#print MonstrousAssistant.toHelper`, `#check
Monster.mk` show Lean's generated constructors; there is nothing to translate:

```lean
structure MythicalCreature where
  large : Bool
deriving Repr

structure Monster extends MythicalCreature where
  vulnerability : String
deriving Repr

def troll : Monster where
  large := true
  vulnerability := "sunlight"

def troll : Monster := {large := true, vulnerability := "sunlight"}

def troll : Monster := ⟨⟨true⟩, "sunlight"⟩

def MythicalCreature.small (c : MythicalCreature) : Bool := !c.large

structure Helper extends MythicalCreature where
  assistance : String
  payment : String
deriving Repr

def nisse : Helper where
  large := false
  assistance := "household tasks"
  payment := "porridge"

structure MonstrousAssistant extends Monster, Helper where
deriving Repr

def domesticatedTroll : MonstrousAssistant where
  large := true
  assistance := "heavy labor"
  payment := "toy goats"
  vulnerability := "sunlight"
```

---

## 5.2 Applicative Functors

The motivating problem: validate a form (`RawInput`) and report **all** errors,
not just the first. `Validate ε α` is "an `α`, or a non-empty list of errors", and
`<*>` combines independent validations while concatenating their errors. In C the
validator is the loop everybody writes: run every check and append to an error
list. The result is the book's `Validate (Field × String) (CheckedInput year)`
as a struct holding both the error list and the (partial) checked value.

```c
typedef const char *Str;

typedef struct { Str name; Str birthYear; } RawInput;
typedef struct { Str name; unsigned birthYear; } CheckedInput;

typedef struct {              /* NonEmptyList (Field × String), here with room for 4 */
  Str field[4];
  Str msg[4];
  unsigned count;
} Errors;

typedef struct { Errors errs; CheckedInput val; } Validated;

Errors reportError(Errors e, Str field, Str msg) {
  e.field[e.count] = field;
  e.msg[e.count] = msg;
  e.count++;
  return e;
}

bool allDigits(Str s) {                      /* part of checkYearIsNat */
  if (strlen(s) == 0) return false;
  for (unsigned i = 0; i < strlen(s); i++)
    if (!isdigit(s[i])) return false;
  return true;
}

unsigned toNat(Str s) {                      /* String.toNat? once allDigits holds */
  unsigned n = 0;
  for (unsigned i = 0; i < strlen(s); i++)
    n = n * 10 + (s[i] - '0');
  return n;
}

Validated checkInput(unsigned thisYear, RawInput input) {
  Errors errs = {{""}, {""}, 0};
  CheckedInput out = {"", 0};

  if (strcmp(input.name, "") == 0)           /* checkName */
    errs = reportError(errs, "name", "Required");
  else
    out.name = input.name;

  if (!allDigits(input.birthYear)) {         /* checkYearIsNat, then checkBirthYear */
    errs = reportError(errs, "birth year", "Must be digits");
  } else {
    unsigned y = toNat(input.birthYear);
    if (y <= 1900) {
      errs = reportError(errs, "birth year", "Must be after 1900");
    } else if (y > thisYear) {
      char msg[64] = "";
      sprintf(msg, "Must be no later than %d", thisYear);
      errs = reportError(errs, "birth year", msg);
    } else {
      out.birthYear = y;
    }
  }

  Validated v = {errs, out};
  return v;
}
```

What is lost: `checkInput` in the book *cannot* build a `CheckedInput` unless every
check passed — the refinement types `{n : String // n ≠ ""}` and
`{y : Nat // y > 1900 ∧ y ≤ thisYear}` carry the proof (**G18**); here a
`CheckedInput` with empty fields is constructible, and the caller must test
`errs.count`. The book's key point — that `Validate` cannot be a `Monad` because
the second check would depend on the first's *success* — is the same in C: the
digit check must precede the range check, so those two *are* sequential
(`andThen`), while the name check is independent.

### ❌ `Applicative` instances, `Pair`, `Subtype`, `Validate`

```lean
instance : Applicative Option where
  pure x := .some x
  seq f x :=
    match f with
    | none => none
    | some g => g <$> x ()

instance : Applicative (Except ε) where
  pure x := .ok x
  seq f x :=
    match f with
    | .error e => .error e
    | .ok g => g <$> x ()

structure Pair (α β : Type) : Type where
  first : α
  second : β

instance : Functor (Pair α) where
  map f x := ⟨x.first, f x.second⟩

structure RawInput where
  name : String
  birthYear : String

structure Subtype {α : Type} (p : α → Prop) where
  val : α
  property : p val

def FastPos : Type := {x : Nat // x > 0}

def one : FastPos := ⟨1, by decide⟩

instance : OfNat FastPos (n + 1) where
  ofNat := ⟨n + 1, by simp⟩

def Nat.asFastPos? (n : Nat) : Option FastPos :=
  if h : n > 0 then
    some ⟨n, h⟩
  else none

structure CheckedInput (thisYear : Nat) : Type where
  name : {n : String // n ≠ ""}
  birthYear : {y : Nat // y > 1900 ∧ y ≤ thisYear}

inductive Validate (ε α : Type) : Type where
  | ok : α → Validate ε α
  | errors : NonEmptyList ε → Validate ε α

instance : Functor (Validate ε) where
  map f
   | .ok x => .ok (f x)
   | .errors errs => .errors errs

instance : Applicative (Validate ε) where
  pure := .ok
  seq f x :=
    match f with
    | .ok g => g <$> (x ())
    | .errors errs =>
      match x () with
      | .ok _ => .errors errs
      | .errors errs' => .errors (errs ++ errs')

def Field := String

def reportError (f : Field) (msg : String) : Validate (Field × String) α :=
  .errors { head := (f, msg), tail := [] }

def checkName (name : String) :
    Validate (Field × String) {n : String // n ≠ ""} :=
  if h : name = "" then
    reportError "name" "Required"
  else pure ⟨name, h⟩

def Validate.andThen (val : Validate ε α)
    (next : α → Validate ε β) : Validate ε β :=
  match val with
  | .errors errs => .errors errs
  | .ok x => next x

def checkYearIsNat (year : String) : Validate (Field × String) Nat :=
  match year.trimAscii.toNat? with
  | none => reportError "birth year" "Must be digits"
  | some n => pure n

def checkBirthYear (thisYear year : Nat) :
    Validate (Field × String) {y : Nat // y > 1900 ∧ y ≤ thisYear} :=
  if h : year > 1900 then
    if h' : year ≤ thisYear then
      pure ⟨year, by simp [*]⟩
    else reportError "birth year" s!"Must be no later than {thisYear}"
  else reportError "birth year" "Must be after 1900"

def checkInput (year : Nat) (input : RawInput) :
    Validate (Field × String) (CheckedInput year) :=
  pure CheckedInput.mk <*>
    checkName input.name <*>
    (checkYearIsNat input.birthYear).andThen fun birthYearAsNat =>
      checkBirthYear year birthYearAsNat
```

The same record without the proofs is a plain C struct:

```c
typedef struct { const char *first; int second; } Pair;
```

---

## 5.3 The Applicative Contract — ❌

The identity, composition, homomorphism and interchange laws are proved by
rewriting chains of `seq`/`>>=` on paper. They are statements *about Lean
programs* and have no C form; a C-level analogue — properties of functions on
fixed-width values — is `VERIFY_FORALL` ([03-interlude-propositions-proofs-indexing.md](03-interlude-propositions-proofs-indexing.md)).

```lean
def map [Applicative f] (g : α → β) (x : f α) : f β :=
  pure g <*> x

class Applicative (f : Type → Type) extends Functor f where
  pure : α → f α
  seq : f (α → β) → (Unit → f α) → f β
  map g x := seq (pure g) (fun () => x)

def seq [Monad m] (f : m (α → β)) (x : Unit → m α) : m β := do
  let g ← f
  let y ← x ()
  pure (g y)

class Monad (m : Type → Type) extends Applicative m where
  bind : m α → (α → m β) → m β
  seq f x :=
    bind f fun g =>
    bind (x ()) fun y =>
    pure (g y)

def notFun : Validate String (Nat → String) :=
  .errors { head := "First error", tail := [] }

def notArg : Validate String Nat :=
  .errors { head := "Second error", tail := [] }
```

---

## 5.4 Alternatives

"Try the company check; if it fails, try the human-before-1970 check; if that
fails, the human-after-1970 check; report all errors only if all fail." In C this
is an `if` chain over the three attempts, each appending to the error list when
it fails. The tagged union `LegacyCheckedInput` becomes an enum tag plus the
union of the fields:

```c
typedef const char *Str;

enum Kind { HumanBefore1970, HumanAfter1970, Company };

typedef struct {
  enum Kind kind;
  unsigned birthYear;       /* unused for Company */
  Str name;
} LegacyCheckedInput;

typedef struct { Str field[8]; Str msg[8]; unsigned count; } Errors;

typedef struct { bool ok; LegacyCheckedInput val; Errors errs; } Result;

Errors reportError(Errors e, Str field, Str msg) {
  e.field[e.count] = field;
  e.msg[e.count] = msg;
  e.count++;
  return e;
}

typedef struct { Str name; Str birthYear; } RawInput;

Result checkLegacyInput(RawInput input) {
  Errors errs = {{""}, {""}, 0};
  LegacyCheckedInput none = {Company, 0, ""};

  if (strcmp(input.birthYear, "FIRM") == 0 && strcmp(input.name, "") != 0) {   /* checkCompany */
    Result r = {true, {Company, 0, input.name}, errs};
    return r;
  }
  errs = reportError(errs, "birth year", "FIRM if a company");
  if (strcmp(input.name, "") == 0) errs = reportError(errs, "name", "Required");

  Result bad = {false, none, errs};
  return bad;
}
```

This is the "first success wins, otherwise all errors" shape of `<|>`. The *typeclass*
`Alternative`/`OrElse`, the lazy second argument (`Unit → …`), and writing
`checkCompany <|> checkHumanBefore1970 <|> checkHumanAfter1970` as an expression
over *any* applicative are Lean-only (G20, G21).

`Many` plus `guard` (backtracking search) is "all `k` ≤ `n` such that …". The C
idiom is the loop, with an output array (the order — `[20, 10, 4, 2]` — is the
same, counting down):

```c
typedef struct { unsigned items[64]; unsigned count; } Divisors;

Divisors evenDivisors(unsigned n) {
  Divisors r = {{0}, 0};
  for (unsigned k = n; k > 0; k--) {
    if (k % 2 == 0 && n % k == 0) {          /* guard (k % 2 = 0); guard (n % k = 0) */
      r.items[r.count] = k;
      r.count++;
    }
  }
  return r;
}

printf("%d", evenDivisors(20).count);        /* the four divisors 20, 10, 4, 2 */
```

(The loop stops at `k = 1`: Lean's `Many.countdown (n + 1)` also yields `0`, which
`guard (n % k = 0)` rejects because `n % 0 = n`, and which C would make an error.)

### ❌ `Alternative`, `Many`, `NonEmptyString`, `TreeError`

```lean
abbrev NonEmptyString := {s : String // s ≠ ""}

inductive LegacyCheckedInput where
  | humanBefore1970 :
    (birthYear : {y : Nat // y > 999 ∧ y < 1970}) →
    NonEmptyString →
    LegacyCheckedInput
  | humanAfter1970 :
    (birthYear : {y : Nat // y > 1970}) →
    NonEmptyString →
    LegacyCheckedInput
  | company :
    NonEmptyString →
    LegacyCheckedInput
deriving Repr

def Validate.orElse
    (a : Validate ε α)
    (b : Unit → Validate ε α) :
    Validate ε α :=
  match a with
  | .ok x => .ok x
  | .errors errs1 =>
    match b () with
    | .ok x => .ok x
    | .errors errs2 => .errors (errs1 ++ errs2)

class OrElse (α : Type) where
  orElse : α → (Unit → α) → α

instance : OrElse (Validate ε α) where
  orElse := Validate.orElse

def checkThat (condition : Bool)
    (field : Field) (msg : String) :
    Validate (Field × String) Unit :=
  if condition then pure () else reportError field msg

def checkCompany (input : RawInput) :
    Validate (Field × String) LegacyCheckedInput :=
  checkThat (input.birthYear == "FIRM")
    "birth year" "FIRM if a company" *>
  .company <$> checkName input.name

def checkSubtype {α : Type} (v : α) (p : α → Prop) [Decidable (p v)]
    (err : ε) : Validate ε {x : α // p x} :=
  if h : p v then
    pure ⟨v, h⟩
  else
    .errors { head := err, tail := [] }

def checkLegacyInput (input : RawInput) :
    Validate (Field × String) LegacyCheckedInput :=
  checkCompany input <|>
  checkHumanBefore1970 input <|>
  checkHumanAfter1970 input

class Alternative (f : Type → Type) extends Applicative f where
  failure : f α
  orElse : f α → (Unit → f α) → f α

instance [Alternative f] : OrElse (f α) where
  orElse := Alternative.orElse

instance : Alternative Option where
  failure := none
  orElse
    | some x, _ => some x
    | none, y => y ()

def Many.orElse : Many α → (Unit → Many α) → Many α
  | .none, ys => ys ()
  | .more x xs, ys => .more x (fun () => orElse (xs ()) ys)

instance : Alternative Many where
  failure := .none
  orElse := Many.orElse

def guard [Alternative f] (p : Prop) [Decidable p] : f Unit :=
  if p then
    pure ()
  else failure

def Many.countdown : Nat → Many Nat
  | 0 => .none
  | n + 1 => .more n (fun () => countdown n)

def evenDivisors (n : Nat) : Many Nat := do
  let k ← Many.countdown (n + 1)
  guard (k % 2 = 0)
  guard (n % k = 0)
  pure k

inductive TreeError where
  | field : Field → String → TreeError
  | path : String → TreeError → TreeError
  | both : TreeError → TreeError → TreeError

instance : Append TreeError where
  append := .both
```

---

## 5.5 Universes — ❌ (G7)

`Type`, `Type 1`, `Prop`, universe polymorphism (`Type u`), `Sum.{u,v}`. Lean's
hierarchy of "types of types" prevents paradoxes; C has one flat world of
value types and no way to express any of it:

```lean
inductive MyList (α : Type) : Type where
  | nil : MyList α
  | cons : α → MyList α → MyList α

inductive MyList (α : Type u) : Type u where
  | nil : MyList α
  | cons : α → MyList α → MyList α

def myListOfNumbers : MyList Nat :=
  .cons 0 (.cons 1 .nil)

def myListOfNat : MyList Type :=
  .cons Nat .nil

def myListOfList : MyList (Type → Type) :=
  .cons MyList .nil

def myListOfNumbers : MyList.{0} Nat :=
  .cons 0 (.cons 1 .nil)

def myListOfNat : MyList.{1} Type :=
  .cons Nat .nil

def myListOfList : MyList.{1} (Type → Type) :=
  .cons MyList.{0} .nil

inductive Sum (α : Type u) (β : Type u) : Type u where
  | inl : α → Sum α β
  | inr : β → Sum α β

def stringOrNat : Sum String Nat := .inl "hello"

def typeOrType : Sum Type Type := .inr Nat

inductive Sum (α : Type u) (β : Type v) : Type (max u v) where
  | inl : α → Sum α β
  | inr : β → Sum α β

def stringOrType : Sum String Type := .inr Nat

def someTruePropositions : List Prop := [
  1 + 1 = 2,
  "Hello, " ++ "world!" = "Hello, world!"
]

def someTruePropositions : List.{0} Prop := [
  1 + 1 = 2,
  "Hello, " ++ "world!" = "Hello, world!"
]
```

---

## 5.6 The Complete Definitions — ❌ (G20, G7)

The full `Functor`, `Pure`, `Seq`, `SeqLeft`, `SeqRight`, `Applicative`, `Bind`
and `Monad` classes, with universe levels and default methods:

```lean
class Functor (f : Type u → Type v) : Type (max (u+1) v) where
  map : {α β : Type u} → (α → β) → f α → f β
  mapConst : {α β : Type u} → α → f β → f α :=
    Function.comp map (Function.const _)

def simpleConst  (x : α) (_ : β) : α := x

class Pure (f : Type u → Type v) : Type (max (u+1) v) where
  pure {α : Type u} : α → f α

class Seq (f : Type u → Type v) : Type (max (u+1) v) where
  seq : {α β : Type u} → f (α → β) → (Unit → f α) → f β

class SeqRight (f : Type u → Type v) : Type (max (u+1) v) where
  seqRight : {α β : Type u} → f α → (Unit → f β) → f β

class SeqLeft (f : Type u → Type v) : Type (max (u+1) v) where
  seqLeft : {α β : Type u} → f α → (Unit → f β) → f α

class Applicative (f : Type u → Type v)
    extends Functor f, Pure f, Seq f, SeqLeft f, SeqRight f where
  map      := fun x y => Seq.seq (pure x) fun _ => y
  seqLeft  := fun a b => Seq.seq (Functor.map (Function.const _) a) b
  seqRight := fun a b => Seq.seq (Functor.map (Function.const _ id) a) b

class Bind (m : Type u → Type v) where
  bind : {α β : Type u} → m α → (α → m β) → m β

class Monad (m : Type u → Type v) : Type (max (u+1) v)
    extends Applicative m, Bind m where
  map      f x := bind x (Function.comp pure f)
  seq      f x := bind f fun y => Functor.map y (x ())
  seqLeft  x y := bind x fun a => bind (y ()) (fun _ => pure a)
  seqRight x y := bind x fun _ => y ()
```
