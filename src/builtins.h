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
    BitVec,    // bv_cat bv_ext bv_bit bv_zext bv_sext bv_rotl bv_rotr bv_slt bv_sle bv_ult bv_ule bv_sdiv
               // bv_sshr: the main-spec §5.4 helpers, pure; typed by check.cpp, spelled by emitter.cpp
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

// The argument positions of a §5.4 helper that must be constants known while translating
// (a width or a bit position): `bv_zext(N, x)` → 0, `bv_ext(x, hi, lo)` → 1 and 2.
bool bitVecConstArg(const std::string& name, size_t k);

// Constants the C headers define as macros (`EXIT_FAILURE`, `EOF`, `INT32_MAX`, …). The
// tool does not expand macros, so it knows these by name; the parser turns each into
// the integer literal of `libraryConstant`'s value (decimal text, a leading `-` when
// negative), which takes the type of wherever it is used like any literal (D19).
bool isLibraryConstant(const std::string& name);
std::string libraryConstant(const std::string& name);

// ---- the pure C library functions of main-spec §12 ---------------------------------
//
// `<math.h>`, `abs`/`min`/`max`, `<string.h>`'s `strlen`/`strcmp`/`strcat`, `<ctype.h>`,
// `assert`. They translate by name — unless the file defines a function of that name,
// which then wins (C programs often write their own `min`/`max`). check.cpp types the
// call (`stdShape`) and marks it `Expr::lib`; emitter.cpp spells it (`stdSpelling`).
enum class StdShape {
    None,
    Math1,     // double f(double)        sqrt exp log sin floor …   (`…f`: the float version)
    Math2,     // double f(double, double) pow atan2                 (`…f`: the float version)
    FloatTest, // bool f(double)          isnan isinf isfinite      (C: int)
    Abs,       // int abs(int)            abs labs llabs
    MinMax,    // T min(T, T)             min max (the macro idiom; operands unify like `a + b`)
    StrLen,    // size_t strlen(s)
    StrCmp,    // int strcmp(a, b)        -1 / 0 / 1 (a generated helper)
    StrCat,    // strcat(dst, src)        a statement: `dst := dst ++ src`
    CharTest,  // bool f(char)            isdigit isalpha isalnum isspace isupper islower (C: int)
    CharMap,   // char f(char)            toupper tolower
    Assert,    // assert(c)               a statement: `assert! c`
};
StdShape stdShape(const std::string& name);
bool stdFloat32(const std::string& name);  // `sqrtf`, `powf`, … work on `float`
// The Lean function a Math1/Math2/FloatTest/CharTest/CharMap/StrLen call becomes, given whether its
// operand is a `float` (`Float.sqrt`, `Float32.sqrt`, `Char.isDigit`, `String.length`).
std::string stdSpelling(const std::string& name, bool float32);

// `stdin`, `stdout`, `stderr`: the three streams every program has.
bool isStandardStream(const std::string& name);
