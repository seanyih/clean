// emitter.h — AST → Lean source text.
//
// Layer: back-end. Input: a TranslationUnit; Output: the complete .lean file
// as a string (header note, `-- c:` traceability comments, `def` lines).
// Idiomatic output per D3: merged parameter groups, minimal parentheses
// (C and Lean agree on precedence for the slice-0 operators).

#pragma once

#include <string>

#include "ast.h"

std::string emit(const TranslationUnit& unit);
