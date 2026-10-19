// lint.h — opt-in "C semantics" lints (main-spec D4, §13.1; decision D18).
//
// Layer: middle-end, after check.cpp. Input: a checked TranslationUnit (every
// expression typed, implicit conversions already inserted as `Cast` nodes).
// Output: the same unit with lint notes appended to `unit.notes`. It never
// changes the tree and never stops the build: clean translates with Lean's
// semantics (D4), and a lint only says "C would behave differently here".
//
// Off by default; main.cpp runs it for `--lint=c-semantics`. Each lint is
// reported once per kind per file (first place, plus a count of the rest).
//
//   W001  `int`-family is unbounded `Int`            W008  exact-width arithmetic, no `int` promotion
//   W002  `Nat` subtraction truncates at 0           W009  `Int` → `Nat` clamps negative values
//   W005  division by zero is 0                      W010  `a[i]!` can panic
//   W007  `char` ↔ integer conversion                W011  string indexing is O(n)
//   W014  `/` `%` on `Int` round differently from C when an operand is negative (D18)
//
// W003, W004, W006 need features that do not exist yet (`long double` is
// spelled away by the parser, no uninitialised locals, no `printf`).

#pragma once

#include "ast.h"

void lint(TranslationUnit& unit);
