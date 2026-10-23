// parser.h — token stream → AST, plus the early checks that keep the tool
// honest (D5/D8: reject, never guess).
//
// Layer: front-end. Input: tokens and the #define'd names. Output: a
// TranslationUnit (syntax only: whether names exist etc. is check.h). The format of a
// printf is cut up by format.h. Grammar:
//
//   file      :=  ( comment | ';' | prototype | function | print )*  EOF
//   prototype :=  type IDENT '(' params ')' ';'
//   function  :=  attr* type IDENT '(' params ')' block
//   attr      :=  '[[' 'lean' '::' 'partial' ']]'
//   type      :=  one or more C type words, resolved by types.h (main-spec §4.1),
//                 or  'bv' '(' (INTLIT | #define'd integer) ')'
//   params    :=  'void' | ε | type IDENT ( ',' type IDENT )*
//   block     :=  '{' stmt* '}'
//   stmt      :=  type IDENT '=' expr ';'              local
//              |  IDENT ('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '&=' | '|=' | '^=' | '<<=' | '>>=') expr ';'
//              |  IDENT ('++' | '--') ';'
//              |  'if' '(' expr ')' body [ 'else' body ]
//              |  'switch' '(' expr ')' '{' group* '}'
//              |  'while' '(' expr ')' body
//              |  'do' body 'while' '(' expr ')' ';'
//              |  'for' '(' init? ';' expr? ';' step? ')' body     (main-spec §6.1)
//              |  print
//              |  'return' expr ';'  |  'break' ';'  |  'continue' ';'
//              |  block  |  ';'
//   init      :=  a local declaration, an assignment or a call (no comma lists:
//                 C's comma operator is ⛔, E103); each part eats its own ';'
//   step      :=  an assignment, '++'/'--' IDENT or a call (no trailing ';')
//   print     :=  'printf' '(' STRINGLIT (',' expr)* ')' ';'     (also at file scope: §8.2)
//              |  'fprintf' '(' ('stderr'|'stdout') ',' STRINGLIT (',' expr)* ')' ';'
//              |  ('puts' | 'putchar') '(' expr ')' ';'
//   body      :=  block | stmt
//   group     :=  ( 'case' ['-'] INTLIT ':' | 'default' ':' )+ stmt*
//   expr      :=  C precedence:  ||  &&  |  ^  &  == !=  < <= > >=  << >>  + -  * / %
//                 unary - ! ~  and the cast '(' type ')' unary
//   primary   :=  INTLIT | FLOATLIT | CHARLIT | true | false | IDENT | IDENT '(' args ')'
//              |  'BV' '(' width ',' INTLIT ')'  | '(' expr ')'
//
// Comments are not part of the grammar: `parse` lifts them out of the token
// stream first, so they can sit anywhere C allows one and never affect what
// parses. Each is re-attached by where it sat: above the statement it
// precedes or sits inside; a header region (the condition of an `if` or
// `switch`, the whole header of a `while`/`do`/`for`, the signature of a
// definition) attaches to the `if` / `switch` / loop / definition itself; the ones before a closing `}` stay inside the block they
// ended (body end, branch end, case group end); comments between groups land
// on whatever reads next (the next case group, the next statement); the ones
// after the last definition end the file. The odd in-signature spots (inside
// a parameter list, between a type and a name, between `return` and its
// value …) get no special treatment: they parse fine but land somewhere
// plausible rather than exactly in place (see spec/impl-spec.md) — do not
// rely on the spot.
//
// Prototypes are dropped (main-spec §7.1). Everything else is a diagnostic.
// Functions come back in source order; toposort.h puts them in Lean order.

#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "ast.h"
#include "lexer.h"

TranslationUnit parse(const std::vector<Token>& tokens,
                      const std::set<std::string>& definedNames,
                      const std::map<std::string, std::string>& definedValues,
                      const std::string& fileName);
