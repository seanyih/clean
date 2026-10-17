// toposort.h — put definitions in the order Lean needs (main-spec §3.2) and
// find recursion.
//
// Layer: middle-end, between check and emitter.
// Input:  a checked TranslationUnit, functions in source order.
// Output: the same unit with functions reordered so every callee precedes its
//         callers; code that is already in a valid order keeps its place.
//         Recursion is annotated, never rejected:
//           * `Function::recursive`  — the function reaches itself via calls;
//           * `Function::mutualGroup` — functions that call each other share
//             an id ≥ 0 and sit next to each other (the emitter wraps them in
//             `mutual … end`).
//         Whether Lean can prove termination is Lean's question (the user
//         answers with `[[lean::partial]]`).
// Error:  one recursive cycle that mixes `[[lean::partial]]` and plain
//         functions: Lean requires all or none inside a `mutual` block.

#pragma once

#include "ast.h"

void sortDefinitions(TranslationUnit& unit);
