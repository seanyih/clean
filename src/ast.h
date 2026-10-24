// ast.h — the tree the parser builds and the emitter prints.
//
// Layer: pure data between parser (producer), check/toposort (readers and
// annotators) and emitter (consumer). No IO. It grows only when a feature
// lands, never "for later".

#pragma once

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "diag.h"  // Loc
#include "types.h" // Type

// An expression.
//   IntLit   text = literal spelling, kept verbatim (`42`, `0xFF`, `0b101`)
//   FloatLit text = Lean spelling of a floating literal (`1.5`, `2e3`)
//   CharLit  text = Lean spelling of a character literal, quotes included (`'a'`)
//   StringLit text = Lean spelling of a string literal, quotes included (`"a\n"`)
//   BoolLit  text = `true` / `false`
//   BvLit    text = the value, `type` the width: `BV(8, 0x63)` → `0x63#8`
//   Ident    text = a parameter or local name
//   Index    `a[i]`: kids [array or string, index]
//   ArrayLit `{1, 2, 3}` (only as the initialiser of an array local): kids = elements,
//            nested lists for `T a[M][N]`
//   UnaryNeg one kid: `-x`
//   BitNot   one kid: `~x`
//   Cast     one kid, `type` = target: `(T)x`, or an implicit conversion
//            inserted by check.cpp (`implicit`)
//   Binary   text = operator (+ - * / % & | ^ << >>), kids [lhs, rhs]
//   Call     text = callee name, kids = arguments; `io` = the callee is an `IO` function
//   Cmp      text = < <= > >= == !=, kids [lhs, rhs]      } "conditions":
//   And, Or  kids [lhs, rhs]                              } only legal where
//   Not      one kid                                      } a test is expected
// In an `if` test, Cmp/And/Or/Not are Lean `Prop`s; where a `bool` is expected
// they are `Bool` values (`decide (a < b)`, `&&`); check.cpp decides which.
// Anywhere else they are refused.
struct Expr {
    enum class Kind {
        IntLit, FloatLit, CharLit, StringLit, BoolLit, BvLit, Ident, Index, ArrayLit,
        UnaryNeg, BitNot, Cast, Binary, Call, Cmp, And, Or, Not
    };
    Kind kind = Kind::IntLit;
    std::string text;
    std::vector<Expr> kids;
    Loc loc;  // start of the expression, for diagnostics
    Type type;  // the C type of the value; set by check.cpp (parser: Cast target, BvLit width)
    bool implicit = false;  // Cast: inserted by check.cpp, not written in the C source
    bool io = false;        // Call: the callee does I/O, so Lean needs `(← f x)` (check.cpp)
};

// One piece of a `printf` format (format.h): literal text, or one conversion.
struct FmtPiece {
    std::string lit;      // conv == 0: the text, in Lean spelling (escapes already re-spelled)
    char conv = 0;        // 0 = literal; else d i (`d`), u, s, c, f (%f %g %e), x, X
    bool padded = false;  // width / flags / precision were written; they are ignored (lint W006)
};

// A C comment kept for the output. `block` = it was `/* … */` (else `// …`).
struct Comment {
    std::string text;  // without the delimiters; may span lines
    bool block = false;
};

struct Stmt;

// One `case`/`default` group of a switch. Labels that directly follow each
// other (`case 0: case 1:`) form one group = one Lean or-pattern.
struct Case {
    std::vector<Expr> labels;  // IntLit, or UnaryNeg of an IntLit
    bool isDefault = false;    // `default:` is one of the labels
    std::vector<Stmt> body;    // without its closing `break;`
    std::vector<Comment> bodyTrailing;  // comments at the group's end: above the stripped `break;`, or before the next label / the switch's `}`
    bool endsInBreak = false;  // the group closed with `break;`
    Loc loc;
};

// A statement. The fields used depend on `kind`:
//   Decl    `T name = expr;`        type, name, expr, mut   (T = any type types.h knows)
//           `T name[N]…;` `= {…};`  type = Array…, dims = the declared sizes (-1 = left out,
//                                   inferred from the list), noInit when there is no `=`
//   Assign  `name = expr;`          name, expr   (`+=`, `++` … are desugared by the parser)
//           `name[i][j] = expr;`    + indices = [i, j]
//   CallStmt `f(args);`             expr = the Call (only legal for a `void` function)
//   If      `if (expr) body else elseBody`   expr, body, elseBody (empty = no else)
//   Return  `return expr;`          expr   (`return;`: noValue)
//   Switch  `switch (expr) {…}`     expr, cases
//   Block   `{ body }`              body
//   While   `while (expr) body`     expr, body   (main-spec §6)
//   DoWhile `do body while (expr);` expr, body   (emitted `repeat … unless expr do break`)
//   For     `for (init; expr; step) body`  (§6.1) — the parser keeps the header parts
//                                   (init/step hold one statement each, or none; the
//                                   condition is always present, `true` when the header
//                                   omits it). check.cpp then either marks it `range`
//                                   (a range-for: the loop variable is `init`'s name,
//                                   the bounds are read from the header when emitting)
//                                   or rewrites it into the general form: init before a
//                                   `while`, the step at the end of the body and before
//                                   every `continue` of this loop (a `Block` around both,
//                                   so the emitter only ever sees `range` Fors, `While`
//                                   and `Block`)
//   Break   `break;`                ends the innermost loop; the parser strips the ones
//                                   that close a switch case (they never reach the tree)
//                                   and rejects the rest (mid-case `break` included: C
//                                   binds those to the switch, which clean cannot express)
//   Continue `continue;`            legal only inside a loop (the parser checks)
//   Print   `printf(…);` `puts(s);` `putchar(c);` `fprintf(stderr, …);`   (main-spec §8)
//           `sprintf(buf, …);` `snprintf(buf, n, …);`   not I/O: `buf := s!"…"` (targets = [buf], limit = n)
//                                   name = the C function, toStderr, pieces + newline (printf
//                                   family), args = the arguments (printf: one per conversion;
//                                   puts/putchar: the one value), script = at file scope
//   Scan    `scanf(fmt, &a, &b);`   name = "scanf"/"fscanf", pieces (whitespace-only literals + the
//           `fscanf(f, fmt, …);`    conversions), targets = the variables read into, one per conversion;
//                                   stream when hasStream (fscanf), else standard input
//   ReadLine `fgets(s, n, f);`      name = the variable `s` receives the line; args = [n]; stream
//   Close   `fclose(f);`            stream
//   Exit    `exit(code);`           expr = the status; the statement never returns, so
//                                   check.cpp counts it as ending its block like `return`
struct Stmt {
    enum class Kind {
        Decl, Assign, CallStmt, If, Return, Switch, Block, Break, Continue, While, DoWhile,
        For, Print, Exit, Scan, ReadLine, Close
    };
    Kind kind = Kind::Return;
    std::string name;
    Type type;                          // Decl: the declared type
    Expr expr;
    std::vector<Stmt> body;
    std::vector<Stmt> elseBody;
    std::vector<Case> cases;
    std::vector<Stmt> init;             // For: the initialiser (none, or one Decl/Assign/CallStmt)
    std::vector<Stmt> step;             // For: the step (none, or one Assign/CallStmt)
    bool range = false;                 // For: check.cpp recognised a range-for (§6.1)
    bool inclusive = false;             // For, range: the condition was `<=` (hi becomes B+1)
    bool mut = false;                 // Decl: assigned again later → `let mut` (set by check.cpp)
    std::vector<long long> dims;      // Decl of an array: the declared sizes, outermost first
    bool noInit = false;              // Decl: `T a[N];` with no `=` (arrays are zero-filled, §4.4)
    std::vector<Expr> indices;        // Assign: the `[i][j]` of an element assignment
    std::string writeBack;            // CallStmt only: the callee writes one of its array
                                      // parameters (OD11 c); this variable receives the returned
                                      // array, emitted `x := f …` instead of `let _ := f …`
    bool noValue = false;             // Return: `return;` in a `void` function
    std::vector<FmtPiece> pieces;     // Print (printf, fprintf): the format, cut up
    bool newline = false;             // Print (printf, fprintf): the format ended in `\n` (cut off)
    bool toStderr = false;            // Print: `fprintf(stderr, …)`
    bool script = false;              // Print: at file scope, becomes `#eval` (§8.2)
    std::vector<Expr> args;           // Print: the arguments after the format; ReadLine: [n]
    Expr stream;                      // the FILE * the statement works on (when hasStream)
    bool hasStream = false;           // Print: fprintf/fputs/fputc to a stream value; Scan: fscanf;
                                      // ReadLine, Close: always. Print/Scan without one use stdout/stdin.
    Expr limit;                       // Print (snprintf): the size n; the text is cut to n-1 characters
    bool hasLimit = false;
    std::vector<std::string> targets; // Print (sprintf, snprintf): the string variable that receives the text.
                                      // Scan: the variable each conversion stores into
    std::vector<Loc> targetLocs;      // Scan: where each target was written
    std::vector<Type> targetTypes;    // Scan: the declared type of each target (check.cpp)
    std::vector<bool> targetSkipWs;   // Scan: whitespace precedes this `%c` in the format (` %c` skips it)
    std::vector<bool> targetAddr;     // Scan: the target was written `&x` (a `%s` string is written bare)
    std::vector<Comment> comments;    // comments the parser attached to this statement
    std::vector<Comment> bodyTrailing; // If: before the then-branch's `}`; Block: before its `}`
    std::vector<Comment> elseTrailing; // If: before the else-branch's `}`
    Loc loc;
};

// Visit every expression that sits directly in a statement (conditions,
// initialisers, right-hand sides, return values, switch scrutinees), in
// source order. Sub-expressions are the caller's business.
template <class F>
void forEachExpr(const std::vector<Stmt>& stmts, F&& f) {
    for (const Stmt& s : stmts) {
        for (const Expr& ix : s.indices) f(ix);
        const bool noExpr = s.kind == Stmt::Kind::Block || s.kind == Stmt::Kind::Break ||
                            s.kind == Stmt::Kind::Continue || s.kind == Stmt::Kind::Print ||
                            s.kind == Stmt::Kind::Scan || s.kind == Stmt::Kind::ReadLine ||
                            s.kind == Stmt::Kind::Close ||
                            (s.kind == Stmt::Kind::Decl && s.noInit) ||
                            (s.kind == Stmt::Kind::Return && s.noValue);
        if (!noExpr) f(s.expr);
        for (const Stmt& part : s.init) {  // a For initialiser (one Decl/Assign/CallStmt)
            for (const Expr& ix : part.indices) f(ix);
            if (!(part.kind == Stmt::Kind::Decl && part.noInit)) f(part.expr);
        }
        for (const Stmt& part : s.step) {  // a For step (one Assign/CallStmt)
            for (const Expr& ix : part.indices) f(ix);
            f(part.expr);
        }
        for (const Expr& a : s.args) f(a);
        if (s.hasStream) f(s.stream);
        if (s.hasLimit) f(s.limit);
        forEachExpr(s.body, f);
        forEachExpr(s.elseBody, f);
        for (const Case& c : s.cases) forEachExpr(c.body, f);
    }
}

// One `T f(T a, T b) { … }`, T any type types.h knows (main-spec §4.1).
struct Function {
    std::string name;
    Type retType;                       // the return type
    std::vector<std::pair<Type, std::string>> params;  // (type, name), in order
    std::vector<Stmt> body;
    std::vector<Comment> comments;      // comments that sat directly above the definition
    std::vector<Comment> bodyTrailing;  // comments before the body's closing `}`
    int defLine = 0;                    // line of the leading `int` (D9: `-- c: file:LINE`)
    bool isPartial = false;             // `[[lean::partial]]` → `partial def`
    std::vector<std::string> mutParams; // parameters the body assigns (check.cpp) → `let mut a := a`
    int writerParam = -1;               // `void` function that writes exactly one array parameter
                                       // (OD11 c, check.cpp): that parameter's index; -1 = none.
                                       // The emitter returns the modified array instead of Unit.
    bool mainArgs = false;              // `int main(int argc, char *argv[])`: params are (argc, argv), emitted as
                                       // `main (args : List String)` plus two lets (check.cpp prepareMain)
    bool isIO = false;                  // prints, or calls something that does; `main` always (check.cpp)
                                       // → `: IO T := do` (main-spec §3.3)
    bool recursive = false;             // reaches itself through calls (toposort.cpp)
    int mutualGroup = -1;               // ≥ 0: member of a `mutual … end` block with this id (toposort.cpp)
};

// One translated file.
struct TranslationUnit {
    std::string fileName;               // base name, e.g. "arith.c", used in comments
    std::vector<Function> functions;    // parser: source order; after sortDefinitions(): callees first
    std::set<std::string> pragmas;      // `#pragma lean NAME` words (set by main from the lexer)
    std::vector<DiagError> notes;       // hints (H…) found by check.cpp; main prints them, they never stop the build
    std::vector<Comment> trailingComments;  // comments after the last definition
    std::vector<Stmt> scripts;          // file-scope `printf` etc. (§8.2), source order; emitted as `#eval` after the functions
    std::set<std::string> helpers;      // generated Lean helpers the file needs, e.g. "hexOf" (check.cpp; prelude.h)
};
