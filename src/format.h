// format.h — a `printf` format string, cut into pieces (main-spec §8.1).
//
// Layer: front-end helper, used by the parser. Input: the body of a C string
// literal in its Lean spelling (what the lexer produced, quotes removed, so
// `\n` is already the two characters `\` `n`). Output: literal pieces and
// conversion pieces, in order. It knows nothing about types or arguments:
// which argument a conversion gets, and whether its type fits, is check.cpp.
//
//   "a=%d, %08x\n"  →  lit "a=", conv d, lit ", ", conv x (padded), newline
//
// A trailing `\n` is cut off and reported as `newline`, so the emitter can
// choose `IO.println` over `IO.print`. `%%` becomes a literal `%`.

#pragma once

#include <string>
#include <vector>

#include "ast.h"  // FmtPiece

struct FormatResult {
    bool ok = false;
    std::vector<FmtPiece> pieces;
    bool newline = false;  // the format ended in `\n` (removed from `pieces`)
    std::string problem;   // when !ok: what, why, fix (the parser adds the location)
    std::string why;
    std::string fix;
};

FormatResult parseFormat(const std::string& body);
