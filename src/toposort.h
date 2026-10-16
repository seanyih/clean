// toposort.h — put definitions in the order Lean needs (main-spec §3.2).
//
// Layer: middle-end, between parser and emitter.
// Input:  a TranslationUnit whose functions are in source order, already
//         checked (every callee exists, arities match).
// Output: the same unit, functions reordered so every callee precedes its
//         callers. Functions that are already in a valid order keep their
//         place; otherwise a function is pulled up only as far as needed.
// Error:  recursion (a function that reaches itself through calls) is
//         rejected: Lean would need `mutual … end` or a termination proof,
//         which this build does not produce yet.

#pragma once

#include "ast.h"

void sortDefinitions(TranslationUnit& unit);
