// parser.h — token stream → AST, plus the early checks that keep the tool
// honest (D5/D8: reject, never guess).
//
// Layer: front-end. Input: tokens and the #define'd names. Output: a
// TranslationUnit. Slice-0 grammar, in full:
//
//   file      :=  ( comment | ';' | prototype | function )*  EOF
//   prototype :=  'int' IDENT '(' params ')' ';'
//   function  :=  'int' IDENT '(' params ')' '{' 'return' expr ';' '}'
//   params    :=  'void' | ε | 'int' IDENT ( ',' 'int' IDENT )*
//   expr      :=  additive with C precedence over + - * / % and unary '-'
//   primary   :=  INTLIT | IDENT | IDENT '(' args ')' | '(' expr ')'
//
// Prototypes are dropped (main-spec §7.1). Everything else is a diagnostic.
// Functions come back in source order; toposort.h puts them in Lean order.

#pragma once

#include <set>
#include <string>
#include <vector>

#include "ast.h"
#include "lexer.h"

TranslationUnit parse(const std::vector<Token>& tokens,
                      const std::set<std::string>& definedNames,
                      const std::string& fileName);
