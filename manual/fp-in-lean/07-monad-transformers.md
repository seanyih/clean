# 6. Monad Transformers — in Clean

Book chapter: <https://lean-lang.org/functional_programming_in_lean/Monad-Transformers/>
Legend and gap IDs: see [README.md](README.md).

Monad transformers (`ReaderT`, `OptionT`, `ExceptT`, `StateT`) stack *effects*
(configuration, failure, errors, state) on top of `IO`. In C there is no stacking
to do, because effects are not tracked in the types: a function reads a parameter,
returns early, and modifies a struct whenever it wants. So the **programs** of this
chapter translate to ordinary C, and the **machinery** (transformers, `MonadLift`,
`MonadState`, `ForM`/`ForIn` instances) does not (**G20**, **G21**). The one thing
that disappears is the book's *ordering* discussion (`StateT` over `Except` vs
`Except` over `StateT`): in a C struct the error and the state are always both
returned, which is the second ordering.

| Section | Status |
| --- | --- |
| 6.1 Combining `IO` and `Reader` (`doug`) | 🟡 config, argument parsing, path-prefix helpers ✅; directory traversal ❌ G15; `ReaderT` ❌ G21 |
| 6.2 A Monad Construction Kit | 🟡 the example programs ✅; transformers/type classes ❌ G20, G21 |
| 6.3 Ordering Monad Transformers | ❌ meaningless in C (see above) |
| 6.4 More `do` Features | ✅ `return`, `unless`, `for`, `while`, `break`/`continue`, `let mut`; `ForM`/`ForIn` classes, parallel `for` ❌ G20, G24 |
| 6.5 Additional Conveniences | 🟡 `repeat`/`while`/`break` ✅ (Lean's `<\|`, pipelines ❌ G5) |

---

## 6.1 Combining `IO` and `Reader` — the `doug` program

`doug` prints a directory tree. Its design problem is that every function needs the
same `Config` (ASCII or Unicode lines, and the current indentation prefix); the
book threads it with a `ReaderT`. In C the same design is "pass the struct", and
the `locally`/`withReader` combinator is "call the recursive function with a
modified *copy* of the struct" (struct arguments are values in Clean).

The configuration and its string helpers:

```c
typedef const char *Str;

typedef struct { bool useASCII; Str currentPrefix; } Config;   /* defaults: {false, ""} */

Str preFile(Config cfg) {
  if (cfg.useASCII) return "|--";
  return "├──";
}

Str preDir(Config cfg) {
  if (cfg.useASCII) return "|  ";
  return "│  ";
}

Str fileName(Config cfg, Str file) {
  char s[256] = "";
  sprintf(s, "%s%s %s", cfg.currentPrefix, preFile(cfg), file);
  return s;
}

Str dirName(Config cfg, Str dir) {
  char s[256] = "";
  sprintf(s, "%s%s %s/", cfg.currentPrefix, preFile(cfg), dir);
  return s;
}

Config inDirectory(Config cfg) {           /* {cfg with currentPrefix := …} */
  char p[256] = "";
  sprintf(p, "%s %s", preDir(cfg), cfg.currentPrefix);
  Config r = {cfg.useASCII, p};
  return r;
}
```

`configFromArgs` and `main`: the book matches the argument *list*
(`[] | ["--ascii"] | _`); C reads `argc`/`argv`:

```c
typedef const char *Str;
typedef struct { bool useASCII; Str currentPrefix; } Config;

const Str usage = "Usage: doug [--ascii]\nOptions:\n\t--ascii\tUse ASCII characters to display the directory structure";

int main(int argc, char *argv[]) {
  Config config = {false, ""};              /* the book's `some {}`: both fields default */

  if (argc == 2 && strcmp(argv[1], "--ascii") == 0) {
    config.useASCII = true;
  } else if (argc != 1) {
    fprintf(stderr, "Didn't understand argument(s)\n");
    fprintf(stderr, "%s\n", usage);
    return 1;
  }
  /* dirTree(config, currentDir) — see below */
  return 0;
}
```

🟡 **G23.** The book's `structure Config where useASCII : Bool := false …`
gives fields *default values*, so `{}` and `{useASCII := true}` fill in the rest.
C needs both fields written, as above.

### ❌ Walking the directory tree (G15) and the `ReaderT` plumbing (G21)

`dirTree` needs file-system queries that have no C-library counterpart in Clean:
`IO.currentDir`, `path.isDir`, `path.components`, `path.readDir`, sorting the
entries — and a recursion over them. Likewise the `ConfigIO` type, written first
as a hand-made monad and then as `ReaderT Config IO`:

```lean
structure Config where
  useASCII : Bool := false
  currentPrefix : String := ""

def usage : String :=
  "Usage: doug [--ascii]
Options:
\t--ascii\tUse ASCII characters to display the directory structure"

def configFromArgs : List String → Option Config
  | [] => some {} -- both fields default
  | ["--ascii"] => some {useASCII := true}
  | _ => none

def main (args : List String) : IO UInt32 := do
  match configFromArgs args with
  | some config =>
    dirTree config (← IO.currentDir)
    pure 0
  | none =>
    IO.eprintln s!"Didn't understand argument(s) {" ".separate args}\n"
    IO.eprintln usage
    pure 1

inductive Entry where
  | file : String → Entry
  | dir : String → Entry

def toEntry (path : System.FilePath) : IO (Option Entry) := do
  match path.components.getLast? with
  | none => pure (some (.dir ""))
  | some "." | some ".." => pure none
  | some name =>
    pure (some (if (← path.isDir) then .dir name else .file name))

partial def dirTree (cfg : Config) (path : System.FilePath) : IO Unit := do
  match ← toEntry path with
  | none => pure ()
  | some (.file name) => showFileName cfg name
  | some (.dir name) =>
    showDirName cfg name
    let contents ← path.readDir
    let newConfig := cfg.inDirectory
    doList (contents.qsort dirLT).toList fun d =>
      dirTree newConfig d.path

def dirLT (e1 : IO.FS.DirEntry) (e2 : IO.FS.DirEntry) : Bool :=
  e1.fileName < e2.fileName

def doList [Applicative f] : List α → (α → f Unit) → f Unit
  | [], _ => pure ()
  | x :: xs, action =>
    action x *>
    doList xs action

def ConfigIO (α : Type) : Type :=
  Config → IO α

instance : Monad ConfigIO where
  pure x := fun _ => pure x
  bind result next := fun cfg => do
    let v ← result cfg
    next v cfg

def ConfigIO.run (action : ConfigIO α) (cfg : Config) : IO α :=
  action cfg

def currentConfig : ConfigIO Config :=
  fun cfg => pure cfg

def locally (change : Config → Config) (action : ConfigIO α) : ConfigIO α :=
  fun cfg => action (change cfg)

def runIO (action : IO α) : ConfigIO α :=
  fun _ => action

def showFileName (file : String) : ConfigIO Unit := do
  runIO (IO.println ((← currentConfig).fileName file))

def showDirName (dir : String) : ConfigIO Unit := do
  runIO (IO.println ((← currentConfig).dirName dir))

partial def dirTree (path : System.FilePath) : ConfigIO Unit := do
  match ← runIO (toEntry path) with
    | none => pure ()
    | some (.file name) => showFileName name
    | some (.dir name) =>
      showDirName name
      let contents ← runIO path.readDir
      locally (·.inDirectory)
        (doList (contents.qsort dirLT).toList fun d =>
          dirTree d.path)

def ReaderT (ρ : Type u) (m : Type u → Type v) (α : Type u) :
    Type (max u v) :=
  ρ → m α

abbrev ConfigIO (α : Type) : Type := ReaderT Config IO α

def read [Monad m] : ReaderT ρ m ρ :=
   fun env => pure env

class MonadReader (ρ : outParam (Type u)) (m : Type u → Type v) :
    Type (max (u + 1) v) where
  read : m ρ

instance [Monad m] : MonadReader ρ (ReaderT ρ m) where
  read := fun env => pure env

export MonadReader (read)

instance [Monad m] : Monad (ReaderT ρ m) where
  pure x := fun _ => pure x
  bind result next := fun env => do
    let v ← result env
    next v env

class MonadLift (m : Type u → Type v) (n : Type u → Type w) where
  monadLift : {α : Type u} → m α → n α

instance : MonadLift m (ReaderT ρ m) where
  monadLift action := fun _ => action

class MonadWithReader (ρ : outParam (Type u)) (m : Type u → Type v) where
  withReader {α : Type u} : (ρ → ρ) → m α → m α

export MonadWithReader (withReader)

instance : MonadWithReader ρ (ReaderT ρ m) where
  withReader change action :=
    fun cfg => action (change cfg)

partial def dirTree (path : System.FilePath) : ConfigIO Unit := do
  match ← toEntry path with
    | none => pure ()
    | some (.file name) => showFileName name
    | some (.dir name) =>
      showDirName name
      let contents ← path.readDir
      withReader (·.inDirectory)
        (doList (contents.qsort dirLT).toList fun d =>
          dirTree d.path)
```

---

## 6.2 A Monad Construction Kit

### `OptionT` — "input that may be missing" (`getUserInfo`)

`getSomeInput` fails on a blank line; `getUserInfo` asks two questions and stops
at the first failure. The C program is early `return` after each read. `failure`
is the `ok = false` stand-in for `none` (G1), and `String.trimAscii` is
approximated by testing for an empty line or a bare newline (🟡 **G8**: no trimming):

```c
typedef const char *Str;

typedef struct { bool ok; Str val; } OptionStr;
typedef struct { Str name; Str favoriteBeetle; } UserInfo;
typedef struct { bool ok; UserInfo val; } OptionUserInfo;

OptionStr getSomeInput(void) {
  char input[256] = "";
  fgets(input, 256, stdin);
  OptionStr r = {false, ""};
  if (strcmp(input, "") == 0 || strcmp(input, "\n") == 0) return r;     /* failure */
  r.ok = true;
  r.val = input;
  return r;
}

OptionUserInfo getUserInfo(void) {
  OptionUserInfo none = {false, {"", ""}};

  puts("What is your name?");
  OptionStr name = getSomeInput();
  if (!name.ok) return none;

  puts("What is your favorite species of beetle?");
  OptionStr beetle = getSomeInput();
  if (!beetle.ok) return none;

  OptionUserInfo r = {true, {name.val, beetle.val}};
  return r;
}

void interact(void) {
  OptionUserInfo info = getUserInfo();
  if (!info.ok) fprintf(stderr, "Missing info\n");
  else printf("Hello %s, whose favorite beetle is %s.\n",
              info.val.name, info.val.favoriteBeetle);
}
```

(`input` keeps its trailing `"\n"`, so the greeting prints it, as in plain C.)

### `ExceptT` — errors with a type (`divFrontend`)

`divBackend` throws `divByZero`; `asNumber` throws `notANumber s`;
`divFrontend` catches both and returns a message. The C equivalent has an enum
tag with an optional payload (G3: a payload in a variant needs a tagged struct)
and `try/catch` is a test after each call. Negative numbers are left out
(`String.toInt?` has no libc counterpart in Clean; digits only):

```c
typedef const char *Str;

enum ErrKind { divByZero, notANumber };

typedef struct { bool ok; int val; enum ErrKind kind; Str bad; } ExceptInt;

bool allDigits(Str s) {
  if (strlen(s) == 0) return false;
  for (unsigned i = 0; i < strlen(s); i++)
    if (!isdigit(s[i])) return false;
  return true;
}

ExceptInt divBackend(int n, int k) {
  ExceptInt r = {true, 0, divByZero, ""};
  if (k == 0) { r.ok = false; r.kind = divByZero; return r; }     /* throw .divByZero */
  r.val = n / k;
  return r;
}

ExceptInt asNumber(Str s) {
  ExceptInt r = {true, 0, divByZero, ""};
  if (!allDigits(s)) { r.ok = false; r.kind = notANumber; r.bad = s; return r; }
  for (unsigned i = 0; i < strlen(s); i++) r.val = r.val * 10 + (s[i] - '0');
  return r;
}

Str divFrontend(Str n, Str k) {
  ExceptInt a = asNumber(n);
  if (!a.ok) { char m[100] = ""; sprintf(m, "Not a number: \"%s\"", a.bad); return m; }
  ExceptInt b = asNumber(k);
  if (!b.ok) { char m[100] = ""; sprintf(m, "Not a number: \"%s\"", b.bad); return m; }
  ExceptInt q = divBackend(a.val, b.val);
  if (!q.ok) return "Division by zero!";             /* catch .divByZero */
  char out[100] = "";
  sprintf(out, "%d", q.val);
  return out;
}
```

### `StateT` over `Except` — `countLetters`

Count vowels and consonants, and fail on the first non-letter. In C the state is a
struct, and the error is a flag in the result; both are returned together (so the
counts so far survive the error — the `ExceptT`-over-`StateT` order of §6.3):

```c
typedef struct { unsigned vowels; unsigned consonants; } LetterCounts;
typedef struct { bool ok; char bad; LetterCounts counts; } CountResult;   /* Err.notALetter bad */

bool isVowel(char c) {                           /* vowels.contains c */
  char l = tolower(c);
  return l == 'a' || l == 'e' || l == 'i' || l == 'u' || l == 'o' || l == 'y';
}

bool isConsonant(char c) {                       /* consonants.contains c */
  char l = tolower(c);
  return l == 'b' || l == 'c' || l == 'd' || l == 'f' || l == 'g' || l == 'h' ||
         l == 'j' || l == 'k' || l == 'l' || l == 'm' || l == 'n' || l == 'p' ||
         l == 'q' || l == 'r' || l == 's' || l == 't' || l == 'v' || l == 'w' ||
         l == 'x' || l == 'z';
}

CountResult countLetters(const char *str, LetterCounts st) {
  CountResult r = {true, 'a', st};
  for (unsigned i = 0; i < strlen(str); i++) {
    char c = str[i];
    if (!isalpha(c)) { r.ok = false; r.bad = c; return r; }     /* throw (.notALetter c) */
    if (isVowel(c)) r.counts.vowels++;                          /* modify … */
    else if (isConsonant(c)) r.counts.consonants++;
  }
  return r;
}
```

### ❌ The transformers themselves (G20, G21)

`OptionT`, `ExceptT`, `StateT`, their `Monad`/`Alternative`/`MonadLift`
instances, and the effect classes `MonadExcept`, `MonadState` that let one
function work in any stack:

```lean
def OptionT (m : Type u → Type v) (α : Type u) : Type v :=
  m (Option α)

def getSomeInput : OptionT IO String := do
  let input ← (← IO.getStdin).getLine
  let trimmed := input.trimAscii
  if trimmed.isEmpty then
    failure
  else pure trimmed.copy

structure UserInfo where
  name : String
  favoriteBeetle : String

def getUserInfo : OptionT IO UserInfo := do
  IO.println "What is your name?"
  let name ← getSomeInput
  IO.println "What is your favorite species of beetle?"
  let beetle ← getSomeInput
  pure ⟨name, beetle⟩

def interact : IO Unit := do
  match ← getUserInfo with
  | none =>
    IO.eprintln "Missing info"
  | some ⟨name, beetle⟩ =>
    IO.println s!"Hello {name}, whose favorite beetle is {beetle}."

instance [Monad m] : Monad (OptionT m) where
  pure x := (pure (some x) : m (Option _))
  bind action next := (do
    match (← action) with
    | none => pure none
    | some v => next v : m (Option _))

structure OptionT (m : Type u → Type v) (α : Type u) : Type v where
  run : m (Option α)

def OptionT.mk (x : m (Option α)) : OptionT m α := x

def OptionT.run (x : OptionT m α) : m (Option α) := x

instance [Monad m] : Monad (OptionT m) where
  pure x := OptionT.mk (pure (some x))
  bind action next := OptionT.mk do
    match ← action with
    | none => pure none
    | some v => next v

instance [Monad m] : Alternative (OptionT m) where
  failure := OptionT.mk (pure none)
  orElse x y := OptionT.mk do
    match ← x with
    | some result => pure (some result)
    | none => y ()

instance [Monad m] : MonadLift m (OptionT m) where
  monadLift action := OptionT.mk do
    pure (some (← action))

def ExceptT (ε : Type u) (m : Type u → Type v) (α : Type u) : Type v :=
  m (Except ε α)

def ExceptT.mk {ε α : Type u} (x : m (Except ε α)) : ExceptT ε m α := x

def ExceptT.run {ε α : Type u} (x : ExceptT ε m α) : m (Except ε α) := x

instance {ε : Type u} {m : Type u → Type v} [Monad m] :
    Monad (ExceptT ε m) where
  pure x := ExceptT.mk (pure (Except.ok x))
  bind result next := ExceptT.mk do
    match ← result with
    | .error e => pure (.error e)
    | .ok x => next x

instance [Monad m] : MonadLift (Except ε) (ExceptT ε m) where
  monadLift action := ExceptT.mk (pure action)

instance [Monad m] : MonadLift m (ExceptT ε m) where
  monadLift action := ExceptT.mk (.ok <$> action)

class MonadExcept (ε : outParam (Type u)) (m : Type v → Type w) where
  throw : ε → m α
  tryCatch : m α → (ε → m α) → m α

inductive Err where
  | divByZero
  | notANumber : String → Err

def divBackend [Monad m] [MonadExcept Err m] (n k : Int) : m Int :=
  if k == 0 then
    throw .divByZero
  else pure (n / k)

def asNumber [Monad m] [MonadExcept Err m] (s : String) : m Int :=
  match s.toInt? with
  | none => throw (.notANumber s)
  | some i => pure i

def divFrontend [Monad m] [MonadExcept Err m] (n k : String) : m String :=
  tryCatch (do pure (toString (← divBackend (← asNumber n) (← asNumber k))))
    fun
      | .divByZero => pure "Division by zero!"
      | .notANumber s => pure s!"Not a number: \"{s}\""

def divFrontend [Monad m] [MonadExcept Err m] (n k : String) : m String :=
  try
    pure (toString (← divBackend (← asNumber n) (← asNumber k)))
  catch
    | .divByZero => pure "Division by zero!"
    | .notANumber s => pure s!"Not a number: \"{s}\""

def StateT (σ : Type u)
    (m : Type u → Type v) (α : Type u) : Type (max u v) :=
  σ → m (α × σ)

instance [Monad m] : Monad (StateT σ m) where
  pure x := fun s => pure (x, s)
  bind result next := fun s => do
    let (v, s') ← result s
    next v s'

structure LetterCounts where
  vowels : Nat
  consonants : Nat
deriving Repr

inductive Err where
  | notALetter : Char → Err
deriving Repr

def vowels :=
  let lowerVowels := "aeiuoy"
  lowerVowels ++ lowerVowels.map (·.toUpper)

def consonants :=
  let lowerConsonants := "bcdfghjklmnpqrstvwxz"
  lowerConsonants ++ lowerConsonants.map (·.toUpper )

def countLetters (str : String) : StateT LetterCounts (Except Err) Unit :=
  let rec loop (chars : List Char) := do
    match chars with
    | [] => pure ()
    | c :: cs =>
      if c.isAlpha then
        if vowels.contains c then
          modify fun st => {st with vowels := st.vowels + 1}
        else if consonants.contains c then
          modify fun st => {st with consonants := st.consonants + 1}
        else -- modified or non-English letter
          pure ()
      else throw (.notALetter c)
      loop cs
  loop str.toList

def modify [MonadState σ m] (f : σ → σ) : m Unit :=
  modifyGet fun s => ((), f s)

class MonadState (σ : outParam (Type u)) (m : Type u → Type v) :
    Type (max (u+1) v) where
  get : m σ
  set : σ → m PUnit
  modifyGet : (σ → α × σ) → m α
```

---

## 6.3 Ordering Monad Transformers — ❌

`StateT σ (ExceptT ε Id)` keeps *no* state after an error; `ExceptT ε (StateT σ Id)`
keeps the state accumulated so far. The `try … catch` fallback of
`countWithFallback` behaves differently in each (`vowels := 2, consonants := 3`
vs `vowels := 4, consonants := 9`). A C function returning `{ok, counts}` is
always the second; the first would be a function that *discards* `counts` on
error — a one-line difference, not a type-level one. Nothing to translate:

```lean
def countLetters [Monad m] [MonadState LetterCounts m] [MonadExcept Err m]
    (str : String) : m Unit :=
  let rec loop (chars : List Char) := do
    match chars with
    | [] => pure ()
    | c :: cs =>
      if c.isAlpha then
        if vowels.contains c then
          modify fun st => {st with vowels := st.vowels + 1}
        else if consonants.contains c then
          modify fun st => {st with consonants := st.consonants + 1}
        else -- modified or non-English letter
          pure ()
      else throw (.notALetter c)
      loop cs
  loop str.toList

abbrev M1 := StateT LetterCounts (ExceptT Err Id)
abbrev M2 := ExceptT Err (StateT LetterCounts Id)

def countWithFallback
    [Monad m] [MonadState LetterCounts m] [MonadExcept Err m]
    (str : String) : m Unit :=
  try
    countLetters str
  catch _ =>
    countLetters "Fallback"
```

---

## 6.4 More `do` Features

This is the section where Clean is closest to the book, since `do` notation is what
Clean *emits*. The imperative features are the C ones.

### Single-branch `if`, `unless`, early `return`

The book's `main` that validates its arguments and reads a name:

```c
int main(int argc, char *argv[]) {
  char name[100] = "";

  if (argc != 1) {                                  /* unless argv == [] do … return 1 */
    fprintf(stderr, "Expected no arguments, but got %d\n", argc - 1);
    return 1;
  }
  puts("How would you like to be addressed?");
  fgets(name, 100, stdin);
  if (strcmp(name, "") == 0) {
    fprintf(stderr, "No name provided\n");
    return 1;
  }
  printf("Hello, %s!\n", name);
  return 0;
}
```

(`stdout.flush` has no Clean spelling; `fflush` is **not** supported — **G25**.)

### Early exit from a search: `List.find?`

`for x in xs do if p x then return x; failure` — the loop with an early `return`,
and the `continue` variant:

```c
typedef struct { bool ok; int val; } OptionInt;

OptionInt find(const int xs[], unsigned n, int target) {
  OptionInt r = {false, 0};
  for (unsigned i = 0; i < n; i++) {
    if (xs[i] != target) continue;                  /* if not (p x) then continue */
    r.ok = true;
    r.val = xs[i];
    return r;                                       /* return x */
  }
  return r;                                         /* failure */
}
```

### Loops: `for`, ranges, mutable state

`for i in 2...5` counts `2, 3, 4` (the new range syntax is half-open); `let mut`
plus `for` is a C accumulator; `for h : i in 0...xs.size` indexing needs no proof
in C (the `a[i]!` panic instead, G18):

```c
void fourToEight(void) {
  for (unsigned i = 2; i < 5; i++) printf("%d\n", i * 2);
}

void printArray(const int xs[], unsigned n) {
  for (unsigned i = 0; i < n; i++) printf("%d:\t%d\n", i, xs[i]);
}

unsigned three(void) {                              /* let mut x := 0; for _ in [1,2,3] do x := x + 1 */
  unsigned x = 0;
  for (unsigned i = 0; i < 3; i++) x++;
  return x;
}

unsigned six(void) {                                /* for y in [1, 2, 3] do x := x + y */
  unsigned x = 0;
  for (unsigned y = 1; y <= 3; y++) x = x + y;
  return x;
}

unsigned count(const int xs[], unsigned n, int target) {   /* List.count (· == target) */
  unsigned found = 0;
  for (unsigned i = 0; i < n; i++)
    if (xs[i] == target) found++;
  return found;
}
```

The book's mutation rules ("variable `found` cannot be mutated" inside a nested
`do`, a local `let rec` helper, or a closure) are errors C does not have, because C
has no nested `do` blocks or closures; Clean's generated Lean is arranged so
they never occur.

### `forM`/`ForIn` for user collections — ❌ (G20, G24)

`AllLessThan` (counting down from `n - 1`) and `LinesOf` (reading a stream line by
line) become `ForIn` instances so that `for i in nums do …` works over them. The
C reading is a plain loop:

```c
void countToThree(int n) {                          /* countToThree 7 prints 6 5 4 3 */
  for (int i = n - 1; i >= 0; i--) {              /* for i in nums do … */
    if (i < 3) break;
    printf("%d\n", i);
  }
}

bool anyAlpha(const char *line) {                   /* line.any (·.isAlpha) */
  for (unsigned i = 0; i < strlen(line); i++)
    if (isalpha(line[i])) return true;
  return false;
}

int main(int argc, char *argv[]) {                  /* forM (LinesOf.mk stdin) … */
  char line[256] = "";

  if (argc != 1) { fprintf(stderr, "Unexpected arguments\n"); return 1; }
  fgets(line, 256, stdin);
  while (strcmp(line, "") != 0) {
    if (anyAlpha(line)) printf("%s", line);
    line = "";                                      /* fgets leaves the old text at end of input */
    fgets(line, 256, stdin);
  }
  return 0;
}
```

`for x in xs, y in ys do` (iterating two collections *in parallel*) has no C `for`
form (one loop variable per loop); the usual C is one index used for both
(**G24**):

```lean
def parallelLoop := do
  for x in ["currant", "gooseberry", "rowan"], y in 'a'...'e' do
    IO.println (x, y)
```

```lean
class ForM (m : Type u → Type v) (γ : Type w₁)
    (α : outParam (Type w₂)) where
  forM (coll : γ) (f : α → m PUnit) : m PUnit

def List.forM [Monad m] : List α → (α → m PUnit) → m PUnit
  | [], _ => pure ()
  | x :: xs, action => do
    action x
    forM xs action

instance [Monad m] : ForM m (List α) α where
  forM := List.forM

def countLetters (str : String) : StateT LetterCounts (Except Err) Unit :=
  forM str.toList fun c => do
    if c.isAlpha then
      if vowels.contains c then
        modify fun st => {st with vowels := st.vowels + 1}
      else if consonants.contains c then
        modify fun st => {st with consonants := st.consonants + 1}
    else throw (.notALetter c)

def Many.forM [Monad m] : Many α → (α → m PUnit) → m PUnit
  | Many.none, _ => pure ()
  | Many.more first rest, action => do
    action first
    forM (rest ()) action

instance [Monad m] : ForM m (Many α) α where
  forM := Many.forM

structure AllLessThan where
  num : Nat

def AllLessThan.forM [Monad m]
    (coll : AllLessThan) (action : Nat → m Unit) :
    m Unit :=
  let rec countdown : Nat → m Unit
    | 0 => pure ()
    | n + 1 => do
      action n
      countdown n
  countdown coll.num

instance [Monad m] : ForM m AllLessThan Nat where
  forM := AllLessThan.forM

structure LinesOf where
  stream : IO.FS.Stream

partial def LinesOf.forM
    (readFrom : LinesOf) (action : String → IO Unit) :
    IO Unit := do
  let line ← readFrom.stream.getLine
  if line == "" then return ()
  action line
  forM readFrom action

instance : ForM IO LinesOf String where
  forM := LinesOf.forM

def main (argv : List String) : IO UInt32 := do
  if argv != [] then
    IO.eprintln "Unexpected arguments"
    return 1
  forM (LinesOf.mk (← IO.getStdin)) fun line => do
    if line.any (·.isAlpha) then
      IO.print line
  return 0

def OptionT.exec [Applicative m] (action : OptionT m α) : m Unit :=
  action *> pure ()

def countToThree (n : Nat) : IO Unit :=
  let nums : AllLessThan := ⟨n⟩
  OptionT.exec (forM nums fun i => do
    if i < 3 then failure else IO.println i)

def countToThree (n : Nat) : IO Unit := do
  let nums : AllLessThan := ⟨n⟩
  for i in nums do
    if i < 3 then break
    IO.println i

instance [Monad m] : ForIn m AllLessThan Nat where
  forIn := ForM.forIn

def List.find? (p : α → Bool) (xs : List α) : Option α := do
  for x in xs do
    if p x then return x
  failure

def List.find? (p : α → Bool) (xs : List α) : Option α := do
  for x in xs do
    if not (p x) then continue
    return x
  failure

def fourToEight : IO Unit := do
  for i in 2...5 do
    IO.println (i * 2)

def printArray [ToString α] (xs : Array α) : IO Unit := do
  for h : i in 0...xs.size do
    IO.println s!"{i}:\t{xs[i]}"

def two : Nat := Id.run do
  let mut x := 0
  x := x + 1
  x := x + 1
  return x

def three : Nat := Id.run do
  let mut x := 0
  for _ in [1, 2, 3] do
    x := x + 1
  return x

def six : Nat := Id.run do
  let mut x := 0
  for y in [1, 2, 3] do
    x := x + y
  return x

def List.count (p : α → Bool) (xs : List α) : Nat := Id.run do
  let mut found := 0
  for x in xs do
    if p x then found := found + 1
  return found
```

### ❌ Predicates that are themselves monadic (G5, G21)

`count` and `countNot` take a *monadic* predicate `p : α → m Bool` and a
`MonadState Nat`; `runCatch`/`throw` implements early return via `ExceptT`.
They need function-typed parameters and effect classes:

```lean
def count [Monad m] [MonadState Nat m] (p : α → m Bool) : List α → m Unit
  | [] => pure ()
  | x :: xs => do
    if ← p x then
      modify (· + 1)
    count p xs

def countNot [Monad m] [MonadState Nat m] (p : α → m Bool) : List α → m Unit
  | [] => pure ()
  | x :: xs => do
    unless ← p x do
      modify (· + 1)
    countNot p xs

def runCatch [Monad m] (action : ExceptT α m α) : m α := do
  match ← action with
  | Except.ok x => pure x
  | Except.error x => pure x

def List.find? (p : α → Bool) : List α → Option α
  | [] => failure
  | x :: xs =>
    runCatch do
      if p x then throw x else pure ()
      monadLift (find? p xs)
```

---

## 6.5 Additional Conveniences

`repeat`, `break` and `while` are C's `while (true)`, `break`, `while`:

```c
void spam(void) {                       /* repeat IO.println "Spam!" */
  while (true) puts("Spam!");
}
```

`dump` — the loop from chapter 2 — in its `repeat`/`break` and `while` forms is the
same C `while`:

```c
void dump(FILE *stream) {
  int c = fgetc(stream);
  while (c != EOF) {
    putchar(c);
    c = fgetc(stream);
  }
}
```

The string-interpolation, `<|`, `|>` and `<$>` conveniences rest on `ToString`
instances and function values (G5, G20):

```lean
#eval some 5 |> toString              -- "(some 5)"

def times3 (n : Nat) : Nat := n * 3

#eval 5 |> times3 |> toString |> ("It is " ++ ·)

#eval ("It is " ++ ·) <| toString <| times3 <| 5

#eval ("It is " ++ ·) (toString (times3 5))

def spam : IO Unit := do
  repeat IO.println "Spam!"

partial def dump (stream : IO.FS.Stream) : IO Unit := do
  let buf ← stream.read bufsize
  if buf.isEmpty then
    pure ()
  else
    let stdout ← IO.getStdout
    stdout.write buf
    dump stream

def dump (stream : IO.FS.Stream) : IO Unit := do
  let stdout ← IO.getStdout
  repeat do
    let buf ← stream.read bufsize
    if buf.isEmpty then break
    stdout.write buf

def dump (stream : IO.FS.Stream) : IO Unit := do
  let stdout ← IO.getStdout
  let mut buf ← stream.read bufsize
  while not buf.isEmpty do
    stdout.write buf
    buf ← stream.read bufsize
```
