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
//   Call     text = callee name, kids = arguments
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
//   Break   `break;`                only transient: the parser strips the legal
//                                   ones from switch cases and rejects the rest
struct Stmt {
    enum class Kind { Decl, Assign, CallStmt, If, Return, Switch, Block, Break };
    Kind kind = Kind::Return;
    std::string name;
    Type type;                          // Decl: the declared type
    Expr expr;
    std::vector<Stmt> body;
    std::vector<Stmt> elseBody;
    std::vector<Case> cases;
    bool mut = false;                 // Decl: assigned again later → `let mut` (set by check.cpp)
    std::vector<long long> dims;      // Decl of an array: the declared sizes, outermost first
    bool noInit = false;              // Decl: `T a[N];` with no `=` (arrays are zero-filled, §4.4)
    std::vector<Expr> indices;        // Assign: the `[i][j]` of an element assignment
    std::string writeBack;            // CallStmt only: the callee writes one of its array
                                      // parameters (OD11 c); this variable receives the returned
                                      // array, emitted `x := f …` instead of `let _ := f …`
    bool noValue = false;             // Return: `return;` in a `void` function
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
                            (s.kind == Stmt::Kind::Decl && s.noInit) ||
                            (s.kind == Stmt::Kind::Return && s.noValue);
        if (!noExpr) f(s.expr);
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
};
