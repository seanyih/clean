// check.h — meaning checks on the parsed tree (main-spec D5: reject, never guess).
//
// Layer: middle-end, after the parser. Input: a TranslationUnit in source
// order. Output: the same unit, annotated (`Stmt::mut`, `Function::mutParams`),
// or a DiagError. After this pass the emitted Lean has no name, arity, scope
// or missing-`return` error left for Lean to find. It checks:
//   * every name is a parameter or a visible local, every callee is defined
//     in the file with the right arity, no name is declared twice in a scope;
//   * comparisons and `&& || !` appear only where a test is expected (`if`
//     condition, operand of another test); everywhere else a value is needed;
//   * every path returns, and nothing follows a `return` in the same block;
//   * switch: one `default` at most, no duplicate labels, no fall-through
//     from a non-empty group (E105);
//   * which locals/parameters are assigned again (they become `let mut`);
//   * I/O (main-spec §8): which functions are `IO` (they print, or call one
//     that does; `main` always), that each `printf`/`puts`/`putchar` argument
//     fits its conversion, `int main` → `UInt32` result, and the file-scope
//     (script-mode) statements.
// Definition order and recursion are NOT checked here: see toposort.h.

#pragma once

#include "ast.h"

void check(TranslationUnit& unit);
