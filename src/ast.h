// ast.h — the tree the parser builds and the emitter prints.
//
// Layer: pure data between parser (producer), check/toposort (readers and
// annotators) and emitter (consumer). No IO. It grows only when a feature
// lands, never "for later".

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "diag.h"  // Loc

// An expression.
//   IntLit   text = literal spelling, kept verbatim (`42`, `0xFF`, `0b101`)
//   Ident    text = a parameter or local name
//   UnaryNeg one kid: `-x`
//   Binary   text = operator (+ - * / %), kids [lhs, rhs]
//   Call     text = callee name, kids = arguments
//   Cmp      text = < <= > >= == !=, kids [lhs, rhs]      } "conditions":
//   And, Or  kids [lhs, rhs]                              } only legal where
//   Not      one kid                                      } a test is expected
// Conditions are Lean `Prop`s, values are `Int`s; check.cpp keeps them apart.
struct Expr {
    enum class Kind { IntLit, Ident, UnaryNeg, Binary, Call, Cmp, And, Or, Not };
    Kind kind = Kind::IntLit;
    std::string text;
    std::vector<Expr> kids;
    Loc loc;  // start of the expression, for diagnostics
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
    bool endsInBreak = false;  // the group closed with `break;`
    Loc loc;
};

// A statement. The fields used depend on `kind`:
//   Decl    `int name = expr;`      name, expr, mut
//   Assign  `name = expr;`          name, expr   (`+=`, `++` … are desugared by the parser)
//   If      `if (expr) body else elseBody`   expr, body, elseBody (empty = no else)
//   Return  `return expr;`          expr
//   Switch  `switch (expr) {…}`     expr, cases
//   Block   `{ body }`              body
//   Break   `break;`                only transient: the parser strips the legal
//                                   ones from switch cases and rejects the rest
struct Stmt {
    enum class Kind { Decl, Assign, If, Return, Switch, Block, Break };
    Kind kind = Kind::Return;
    std::string name;
    Expr expr;
    std::vector<Stmt> body;
    std::vector<Stmt> elseBody;
    std::vector<Case> cases;
    bool mut = false;                 // Decl: assigned again later → `let mut` (set by check.cpp)
    std::vector<Comment> comments;    // comments directly above the statement
    Loc loc;
};

// Visit every expression that sits directly in a statement (conditions,
// initialisers, right-hand sides, return values, switch scrutinees), in
// source order. Sub-expressions are the caller's business.
template <class F>
void forEachExpr(const std::vector<Stmt>& stmts, F&& f) {
    for (const Stmt& s : stmts) {
        if (s.kind != Stmt::Kind::Block && s.kind != Stmt::Kind::Break) f(s.expr);
        forEachExpr(s.body, f);
        forEachExpr(s.elseBody, f);
        for (const Case& c : s.cases) forEachExpr(c.body, f);
    }
}

// One `int f(int a, int b) { … }`.
struct Function {
    std::string name;
    std::vector<std::pair<std::string, std::string>> params;  // (C type, name), in order
    std::vector<Stmt> body;
    std::vector<Comment> comments;      // comments that sat directly above the definition
    int defLine = 0;                    // line of the leading `int` (D9: `-- c: file:LINE`)
    bool isPartial = false;             // `[[lean::partial]]` → `partial def`
    std::vector<std::string> mutParams; // parameters the body assigns (check.cpp) → `let mut a := a`
    bool recursive = false;             // reaches itself through calls (toposort.cpp)
    int mutualGroup = -1;               // ≥ 0: member of a `mutual … end` block with this id (toposort.cpp)
};

// One translated file.
struct TranslationUnit {
    std::string fileName;               // base name, e.g. "arith.c", used in comments
    std::vector<Function> functions;    // parser: source order; after sortDefinitions(): callees first
};
