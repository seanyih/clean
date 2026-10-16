// lexer.h — C source text → flat token stream.
//
// Layer: front-end. Input: the whole file as text. Output: a Token list.
// Preprocessor lines (#include, #define, …) are consumed here and never
// tokenized; `#define`d names are collected so the parser can reject their
// use — slice 0 does not expand macros, and silently emitting a macro name
// as a Lean identifier would produce broken Lean (D5).

#pragma once

#include <set>
#include <string>
#include <vector>

#include "diag.h"

struct Token {
    enum class Kind { End, Ident, IntLit, Comment, Punct, Other };
    Kind kind = Kind::End;
    // Ident   → the name
    // IntLit  → literal spelling ("42", "0xFF")
    // Comment → comment text without the // or /* */ delimiters (may span lines)
    // Punct   → one of ( ) { } , ; + - * / %
    // Other   → something the lexer saw but slice 0 does not translate
    //           (= < "x" 'c' [ ] . …); still a token so the parser can point at it
    std::string text;
    Loc loc;
};

struct LexResult {
    std::vector<Token> tokens;
    std::set<std::string> definedNames;  // names seen in `#define NAME …`
};

LexResult lex(const std::string& source, const std::string& fileName);
