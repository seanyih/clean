# 2. Hello, World! — in Clean

Book chapter: <https://lean-lang.org/functional_programming_in_lean/Hello___-World___/>
Legend and gap IDs: see [README.md](README.md).

| Section | Status |
| --- | --- |
| 2.1 Running a Program | ✅ |
| 2.2 Step By Step | 🟡 reading/printing ✅; *`IO` actions as values* ❌ G5 |
| 2.3 Starting a Project | n/a — build tooling, not code (see below) |
| 2.4 Worked Example: `cat` | 🟡 works with `fgetc`; byte-chunk `read`, `Option` result and error continuation ❌ G1, G15 |
| 2.5 Additional Conveniences | ✅ nested actions are C's normal call syntax; one refusal on purpose |
| 2.6 Summary | — |

---

## 2.1 Running a Program

`def main : IO Unit := IO.println "Hello, world!"` is the C `main`.
C's `main` returns an exit code, so Clean emits `def main : IO UInt32`
(main-spec §7.1); a `void main(void)` gives `IO Unit`.

```c
#include <stdio.h>

int main(void) {
  puts("Hello, world!");
  return 0;
}
```

or, closer to the book's `IO Unit`:

```c
#include <stdio.h>

void main(void) {
  puts("Hello, world!");
}
```

Run it the Lean way (`lean --run Hello.lean`) after translating:

```bash
release/clean hello.c -o Hello.lean
lean --run Hello.lean
```

A function becomes an `IO` function exactly when it prints, reads, or calls an
`IO` function; the C programmer never writes the type (main-spec §3.3).

### 2.1.4 Combining `IO` actions — the greeting program

Streams: `stdin`, `stdout`, `stderr` are `FILE *` values (`IO.getStdin`,
`IO.getStdout`, …). The book prints a prompt, reads a line and trims it:

```c
#include <stdio.h>

int main(void) {
  char input[256] = "";

  fprintf(stdout, "How would you like to be addressed?\n");
  fgets(input, 256, stdin);
  fprintf(stdout, "Hello, %s!\n", input);
  return 0;
}
```

🟡 **G8.** `fgets` keeps the trailing newline (as in C), and the book removes it
with `input.dropEndWhile Char.isWhitespace`. Clean has no string-slice
operations, so the trimmed `name` cannot be written; `scanf("%s", name)` reads
one whitespace-free word and avoids the problem (W015: it also consumes the
whitespace that follows):

```c
#include <stdio.h>

int main(void) {
  char name[100] = "";

  puts("How would you like to be addressed?");
  scanf("%s", name);
  printf("Hello, %s!\n", name);
  return 0;
}
```

The original, with the trimming step that has no C spelling:

```lean
def main : IO Unit := do
  let stdin ← IO.getStdin
  let stdout ← IO.getStdout

  stdout.putStrLn "How would you like to be addressed?"
  let input ← stdin.getLine
  let name := input.dropEndWhile Char.isWhitespace

  stdout.putStrLn s!"Hello, {name}!"

#eval "Hello!!!".dropEndWhile (· == '!')
#eval "Hello...   ".dropEndWhile (fun c => not (c.isAlphanum))
```

---

## 2.2 Step By Step

The `let x ← action` lines of the book are C assignments from reading calls
(`scanf`, `fgets`, `getchar`). Clean inserts `←` and `(← …)` itself.
`s!"Hello, {name}!"` is `printf("Hello, %s!", name)`.

### 2.2.4 `IO` actions as values — ❌ not expressible (G5)

The central idea of the chapter is that `IO Unit` is a *value* that can be named,
stored in a `List`, passed as an argument and run later. In C a call *is* its
execution; Clean has no function values (**E107**) and no `List`. What can be
written is each action as a function that is called when it should run:

```c
#include <stdio.h>

void shy(void)   { puts("shy"); }

void twice(void) { shy(); shy(); }              /* twice (IO.println "shy") */

void nTimes(unsigned n) {                       /* nTimes (IO.println "shy") n */
  for (unsigned i = 0; i < n; i++) shy();
}

void countdown(unsigned n) {                    /* runActions (countdown n) */
  while (n > 0) {
    printf("%d\n", n);
    n--;
  }
  puts("Blast off!");
}

int main(void) {
  countdown(5);
  return 0;
}
```

The program is *behaviourally* the book's `main := runActions from5`, but the
point of the original — building the list of actions first (`from5.length`) and
running it afterwards — is lost. The original code:

```lean
def twice (action : IO Unit) : IO Unit := do
  action
  action

twice (IO.println "shy")

def nTimes (action : IO Unit) : Nat → IO Unit
  | 0 => pure ()
  | n + 1 => do
    action
    nTimes action n

def countdown : Nat → List (IO Unit)
  | 0 => [IO.println "Blast off!"]
  | n + 1 => IO.println s!"{n + 1}" :: countdown n

def from5 : List (IO Unit) := countdown 5

#eval from5.length

def runActions : List (IO Unit) → IO Unit
  | [] => pure ()
  | act :: actions => do
    act
    runActions actions

def main : IO Unit := runActions from5
```

### 2.2.5 Exercise — an action that is defined but not run

```lean
def main : IO Unit := do
  let englishGreeting := IO.println "Hello!"
  IO.println "Bonjour!"
  englishGreeting
```

In C the statement `englishGreeting` would not even parse as "an action": there
is no way to *bind* a print without performing it, so the exercise has no
translation.

---

## 2.3 Starting a Project — n/a

Lake, `lakefile.toml`, `lake build` and `import` are the build tooling of the
Lean side. Clean's counterpart is one command per file:

```bash
release/clean x.c -o X.lean        # translate (one .lean per .c file)
lean --run X.lean                  # or:  lake build, once X.lean is in a project
```

`import` lines are produced automatically (only `Std.Tactic.BVDecide`, when a
theorem needs it — main-spec §3.2). C's `#include` is dropped. There is no
Clean-side notion of a library or of several modules importing each other
(**G16**: no multi-file projects; the preprocessor's `#include` of one `.c` into
another is the only sharing).

---

## 2.4 Worked Example: `cat`

The book's `feline`: copy standard input or the named files to standard output.

### Concatenating streams

The book reads a 20 KiB buffer at a time (`stream.read bufsize`), which is a
`ByteArray`. Clean's streams support `fgets` (lines) and `fgetc` (one byte):

```c
#include <stdio.h>

void dump(FILE *stream) {
  int c = fgetc(stream);
  while (c != EOF) {
    putchar(c);
    c = fgetc(stream);
  }
}
```

(`partial def dump` loops until the buffer is empty; Clean's `while` is Lean's
`while`, which is `partial` under the hood.)

### Handling input and `main`

The book's `process`/`fileStream` match on `"-" :: args`. C reads the arguments
from `argv`; `main(argc, argv)` becomes `def main (args : List String)` with
`argv` rebuilt (main-spec §7.1):

```c
#include <stdio.h>
#include <string.h>

void dump(FILE *stream) {
  int c = fgetc(stream);
  while (c != EOF) {
    putchar(c);
    c = fgetc(stream);
  }
}

int main(int argc, char *argv[]) {
  int exitCode = 0;

  if (argc == 1) {                    /* no arguments: behave as `feline -` */
    dump(stdin);
    return exitCode;
  }
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-") == 0) {
      dump(stdin);
    } else {
      FILE *f = fopen(argv[i], "r");
      dump(f);
      fclose(f);
    }
  }
  return exitCode;
}
```

Messages to standard error use `fprintf(stderr, …)` (`IO.eprintln`):

```c
#include <stdio.h>

int main(void) {
  fprintf(stderr, "File not found: %s\n", "no-such-file");
  return 1;
}
```

### ❌ What is lost: `Option`-returning `fileStream`, exit code `1`, `Stream` records (G1, G15, G5)

In the book a missing file prints `File not found: …` and the loop *continues*
with exit code `1`. That needs `fileStream : … → IO (Option IO.FS.Stream)`:

* `fopen` returning `NULL` and a test `if (f == NULL)` are refused by design —
  a failing `fopen` stops the program with an error (lint W016, open decision);
  `NULL` and stream comparisons are rejected;
* there is no `Option`, so the result cannot be returned from a helper
  function (G1);
* checking existence first (`filename.pathExists`) has no C spelling (G15:
  file-system queries beyond `fopen`/`fclose`);
* `structure Stream` is a record of *functions* (G5).

```lean
def bufsize : USize := 20 * 1024

partial def dump (stream : IO.FS.Stream) : IO Unit := do
  let buf ← stream.read bufsize
  if buf.isEmpty then
    pure ()
  else
    let stdout ← IO.getStdout
    stdout.write buf
    dump stream

structure Stream where
  flush   : IO Unit
  read    : USize → IO ByteArray
  write   : ByteArray → IO Unit
  getLine : IO String
  putStr  : String → IO Unit
  isTty   : BaseIO Bool

def fileStream (filename : System.FilePath) : IO (Option IO.FS.Stream) := do
  let fileExists ← filename.pathExists
  if not fileExists then
    let stderr ← IO.getStderr
    stderr.putStrLn s!"File not found: {filename}"
    pure none
  else
    let handle ← IO.FS.Handle.mk filename IO.FS.Mode.read
    pure (some (IO.FS.Stream.ofHandle handle))

def process (exitCode : UInt32) (args : List String) : IO UInt32 := do
  match args with
  | [] => pure exitCode
  | "-" :: args =>
    let stdin ← IO.getStdin
    dump stdin
    process exitCode args
  | filename :: args =>
    let stream ← fileStream ⟨filename⟩
    match stream with
    | none =>
      process 1 args
    | some stream =>
      dump stream
      process exitCode args

def main (args : List String) : IO UInt32 :=
  match args with
  | [] => process 0 ["-"]
  | _ =>  process 0 args
```

---

## 2.5 Additional Conveniences

### Nested actions

The book introduces `(← IO.getStdout).write buf` so that an action can be used
where an expression is expected. C already has nested calls in expressions;
Clean writes `(← …)` for you:

```c
#include <stdio.h>

int getNumA(void) { puts("A"); return 5; }

void test(void) {
  if (getNumA() == 5) puts("five");
  printf("%d\n", getNumA() + 1);
}
```

The book's cautionary example is a nested action inside a *branch* of an `if`:

```lean
def getNumA : IO Nat := do
  (← IO.getStdout).putStrLn "A"
  pure 5

def getNumB : IO Nat := do
  (← IO.getStdout).putStrLn "B"
  pure 7

def test : IO Unit := do
  let a : Nat := if (← getNumA) == 5 then 0 else (← getNumB)
  (← IO.getStdout).putStrLn s!"The answer is {a}"
-- error: Nested action `← getNumB` must be nested inside a `do` expression.
```

C evaluates only the chosen branch, which Lean's `(← getNumB)` would not (it
runs first). Clean therefore **refuses** the same program, with the same
reasoning, and points to the same fix as the book (bind first):

<!-- clean: skip -->
```c
void test(void) {
  int a = getNumA() == 5 ? 0 : getNumB();      /* REJECTED: an I/O call inside a branch of ?: */
  printf("The answer is %d\n", a);
}
```

```c
#include <stdio.h>

int getNumA(void) { puts("A"); return 5; }
int getNumB(void) { puts("B"); return 7; }

void test(void) {
  int x = getNumA();
  int y = getNumB();
  int a = x == 5 ? 0 : y;
  printf("The answer is %d\n", a);
}
```

(The same applies to an I/O call on the right of `&&` / `||`.)

### Flexible layouts for `do`

`do { … ; … }`, whitespace layout and `;` on one line are formatting choices of
the Lean file Clean *generates*; the C source has its own layout. Nothing to
translate.

### Running `IO` actions with `#eval`

Only `printf`/`puts`/… are allowed as file-scope statements (they become
`#eval`). Calling a user-defined action at file scope is **not** supported
(**G17**); raw Lean is the escape hatch:

```c
#include <stdio.h>

void countdown(unsigned n) {
  while (n > 0) { printf("%d\n", n); n--; }
  puts("Blast off!");
}

LEAN_DECL("#eval countdown 3");
```

```lean
#eval runActions (countdown 3)
```

---

## 2.6 Summary — evaluation vs execution

The summary's ideas map as follows.

| Book | Clean |
| --- | --- |
| evaluation (pure) vs execution (`IO`) | a C function is a pure Lean `def` until it prints/reads; then it is `IO` — decided by the translator (main-spec §3.3) |
| `do` notation | every function body that uses statements |
| `partial` definitions | `[[lean::partial]]` on the function |
| `lean --run`, `lake build` | `release/clean x.c -o X.lean` then the same Lean commands |
