// parser.h — token stream → AST, plus the early checks that keep the tool
// honest (D5/D8: reject, never guess).
//
// Layer: front-end. Input: tokens and the #define'd names. Output: a
// TranslationUnit (syntax only: whether names exist etc. is check.h). Grammar:
//
//   file      :=  ( comment | ';' | prototype | function )*  EOF
//   prototype :=  type IDENT '(' params ')' ';'
//   function  :=  attr* type IDENT '(' params ')' block
//   attr      :=  '[[' 'lean' '::' 'partial' ']]'
//   type      :=  one or more C type words, resolved by types.h (main-spec §4.1)
//   params    :=  'void' | ε | type IDENT ( ',' type IDENT )*
//   block     :=  '{' stmt* '}'
//   stmt      :=  type IDENT '=' expr ';'              local
//              |  IDENT ('=' | '+=' | '-=' | '*=' | '/=' | '%=') expr ';'
//              |  IDENT ('++' | '--') ';'
//              |  'if' '(' expr ')' body [ 'else' body ]
//              |  'switch' '(' expr ')' '{' group* '}'
//              |  'return' expr ';'  |  'break' ';'  |  block  |  ';'
//   body      :=  block | stmt
//   group     :=  ( 'case' ['-'] INTLIT ':' | 'default' ':' )+ stmt*
//   expr      :=  C precedence:  ||  &&  == !=  < <= > >=  + -  * / %  unary - !
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
