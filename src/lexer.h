// lexer.h — C source text → flat token stream.
//
// Layer: front-end. Input: the whole file as text. Output: a Token list.
// Preprocessor lines (#include, #define, …) are consumed here and never
// tokenized; `#define`d names are collected so the parser can reject their
// use — macros are not expanded, and silently emitting a macro name
// as a Lean identifier would produce broken Lean (D5).

#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "diag.h"

struct Token {
    enum class Kind { End, Ident, IntLit, FloatLit, CharLit, StringLit, Comment, Punct, Other };
    Kind kind = Kind::End;
    // Ident   → the name
    // IntLit  → literal spelling ("42", "0xFF", "0b101")
    // FloatLit→ Lean spelling of a floating literal ("1.5", "0.5", "2e3"; suffix dropped)
    // CharLit → Lean spelling of a character literal, quotes included ("'a'", "'\\n'")
    // StringLit → Lean spelling of a string literal, quotes included ("\"abc\\n\"")
    // Comment → comment text without the // or /* */ delimiters (may span lines)
    // Punct   → one of ( ) { } [ ] , ; : + - * / % < > = ! & | ^ ~
    //           or a two/three-character operator: <= >= == != && || ++ -- += -= *= /= %=
    //           << >> &= |= ^= <<= >>=
    // Other   → something the lexer saw but this build does not translate
    //           (? . …); still a token so the parser can point at it
    std::string text;
    Loc loc;
    bool block = false;  // Comment only: true for `/* … */`, false for `// …`
};

struct LexResult {
    std::vector<Token> tokens;
    std::set<std::string> definedNames;  // names seen in `#define NAME …`
    // `#define NAME 8` — object-like macros whose whole body is one integer
    // literal. Used only for `bv(NAME)` widths (E302 otherwise); using a macro
    // anywhere else is still refused.
    std::map<std::string, std::string> definedValues;
    std::set<std::string> pragmas;  // `#pragma lean allow_bool_array` → "allow_bool_array"
};

LexResult lex(const std::string& source, const std::string& fileName);
