// prelude.h — the Lean helper definitions a translation may need.
//
// Layer: shared knowledge, no IO. A few C library functions have no one-line
// Lean spelling (`%x` needs a hex printer, `scanf` needs a word reader), so the
// output starts with small `def`s for the ones the file uses and only those.
//
//   check.cpp    records the helper names a function needs (`unit.helpers`) and
//                refuses a user function that has such a name;
//   emitter.cpp  prints `helperDefinitions(unit.helpers)` above the functions.
//
// The Lean text of every helper lives in prelude.cpp, nowhere else.

#pragma once

#include <set>
#include <string>
#include <vector>

// The helpers to print for `used`: those and everything they depend on, in
// definition order (a helper comes after the ones it calls).
std::vector<std::string> helperClosure(const std::set<std::string>& used);

// The Lean text for `names` (as returned by helperClosure): one blank line
// between helpers, each with a leading `--` comment saying what it is for.
std::string helperDefinitions(const std::vector<std::string>& names);
