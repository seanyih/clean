// builtins.h — the C library names `clean` translates by itself (main-spec §8, §12).
//
// Layer: shared knowledge, no IO — the sibling of types.h for functions. The
// parser asks "which statement does this name start?", the checker asks "may
// the user define or call this name, and what does it return?". Nobody else
// spells a library name.
//
// To add a library function: a row in `classifyLibName` (and, when it is an
// expression with a fixed signature, in `libSignature`), then the three layers
// as for any statement (spec/how-to-extend.md, Recipe 3).

#pragma once

#include <string>
#include <vector>

#include "types.h"

enum class LibName {
    None,      // an ordinary identifier
    Output,    // printf puts putchar fprintf fputs fputc putc sprintf snprintf → Stmt::Kind::Print (§8.1)
    Exit,      // exit                                           → Stmt::Kind::Exit
    Scan,      // scanf fscanf                                   → Stmt::Kind::Scan
    ReadLine,  // fgets                                          → Stmt::Kind::ReadLine
    Close,     // fclose                                         → Stmt::Kind::Close
    Value,     // getchar fgetc getc fopen: calls that give a value → Expr::Kind::Call (`libSignature`)
};

LibName classifyLibName(const std::string& name);

// True for every name above: a user function of that name would never be
// called (the call is translated as the library function), so check.cpp
// refuses the definition.
bool isLibraryName(const std::string& name);

// The `Value` functions: parameter types and result. All of them are `IO`
// actions (they touch a stream), so a function that calls one is `IO` too.
struct LibSignature {
    std::vector<Type> params;
    Type ret;
};
bool libSignature(const std::string& name, LibSignature& out);

// Constants the C headers define as macros (`EXIT_FAILURE`, `EOF`, …). The tool
// does not expand macros, so it knows these few by name; the parser turns each
// into the integer expression of `libraryConstant`'s value.
bool isLibraryConstant(const std::string& name);
long long libraryConstant(const std::string& name);

// `stdin`, `stdout`, `stderr`: the three streams every program has.
bool isStandardStream(const std::string& name);
