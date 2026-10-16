// ast.h — the tree the parser builds and the emitter prints.
//
// Layer: pure data between parser (producer) and emitter (consumer). No IO.
// This is the slice-0 shape: it grows only when a feature lands, never
// "for later".

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "diag.h"  // Loc

// An expression. Slice-0 kinds only.
//   IntLit   text = literal spelling, kept verbatim (decimal `42`, hex `0xFF`)
//   Ident    text = a parameter name
//   UnaryNeg one kid: `-x`
//   Binary   text = operator (+ - * / %), two kids: [lhs, rhs]
//   Call     text = callee name, kids = arguments
struct Expr {
    enum class Kind { IntLit, Ident, UnaryNeg, Binary, Call };
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

// One `int f(int a, int b) { return …; }`.
struct Function {
    std::string name;
    std::vector<std::pair<std::string, std::string>> params;  // (C type, name), in order
    Expr body;                          // the single return expression
    std::vector<Comment> comments;      // comments that sat directly above the definition
    int defLine = 0;                    // line of the leading `int` (D9: `-- c: file:LINE`)
};

// One translated file.
struct TranslationUnit {
    std::string fileName;               // base name, e.g. "arith.c", used in comments
    std::vector<Function> functions;    // parser: source order; after sortDefinitions(): callees first
};
