// types.h — everything `clean` knows about C data types, in one place.
//
// Layer: shared knowledge, no IO. The parser asks it "what type do these C
// words spell?", the checker asks it "may these two types meet in `a + b`?
// is this cast legal? what is the type of the result?", the emitter asks it
// "how is this type, test or conversion written in Lean?". No other file may
// mention a C type name or a Lean type name.
//
// To add a datatype, see spec/how-to-extend.md (Recipe 1). In short: a `Kind`
// here, its C spelling in types.cpp, and a `case` in each function below —
// the compiler's -Wswitch lists the ones you missed.

#pragma once

#include <string>
#include <vector>

struct Type {
    enum class Kind {
        Literal,       // an integer literal Lean has not pinned to a type yet (`42`)
        FloatLiteral,  // a floating literal not pinned yet (`1.5`)
        Int,           // C `int`, `signed`, `short`, `long`, `long long`   → `Int`
        Nat,           // C `unsigned …`, `size_t`                          → `Nat`
        SInt,          // C `intN_t`, width = N                             → `IntN`
        UInt,          // C `uintN_t`, `unsigned char`, width = N           → `UIntN`
        BitVec,        // C `bv(N)`, width = N                              → `BitVec N`
        Float32,       // C `float`                                         → `Float32`
        Float64,       // C `double`, `long double`                         → `Float`
        Bool,          // C `bool`, `_Bool`                                 → `Bool`
        Char,          // C `char`                                          → `Char`
    };
    Kind kind = Kind::Int;
    int width = 0;  // SInt/UInt: 8 16 32 64; BitVec: N; otherwise 0

    bool operator==(const Type& o) const { return kind == o.kind && width == o.width; }
    bool operator!=(const Type& o) const { return !(*this == o); }
    static Type of(Kind k, int w = 0) {
        Type t;
        t.kind = k;
        t.width = w;
        return t;
    }
};

bool isLiteral(Type t);   // Literal or FloatLiteral
bool isFloat(Type t);     // Float32 / Float64 (not the literal)

// The Lean spelling: `Int`, `UInt8`, `BitVec 8`, `Float`, `Bool`, …
std::string leanName(Type t);
// The C spelling, for diagnostics: `int`, `uint8_t`, `bv(8)`, `double`, …
std::string cName(Type t);

// ---- reading a type out of C source ----------------------------------------

enum class TypeWord {
    NotAType,   // an ordinary identifier
    Supported,  // part of a type clean translates (`int`, `unsigned`, `uint8_t`, `bool`, …)
    Refused,    // a C type / storage word clean knows but does not translate
};
TypeWord classifyTypeWord(const std::string& word);

// The words of ONE type specifier, in source order (`{"unsigned","long","long"}`).
// `bv(N)` is not here: the parser reads its argument and calls makeBitVec.
struct TypeResolution {
    bool ok = false;
    Type type;
    std::string code = "E000";
    std::string problem;
    std::string why;
    std::string fix;
};
TypeResolution resolveTypeWords(const std::vector<std::string>& words);
TypeResolution makeBitVec(long long width);  // E302 if it is not a usable constant

// True for the two spellings of the bit-vector type (`bv`, `_BitVec`).
bool isBitVecWord(const std::string& word);

// "`int` or `unsigned`"-style hint for messages, so they never go stale.
std::string supportedTypesHint();

// ---- expressions ---------------------------------------------------------------

// The common type of two operands (`a + b`, `a < b`, …).
//   Literal adapts to the other side; `Int` with `Nat` gives `Int` (Lean
//   coerces, D4); anything else must match exactly, or the operands are
//   refused: Lean has no implicit conversions between the other types.
struct Unified {
    bool ok = false;
    Type type;
    std::string code = "E000";
    std::string problem;
    std::string why;
    std::string fix;
};
Unified unify(Type a, Type b);

// Is the C operator (`+ - * / % & | ^ << >>`, unary `-` `~`) defined on this type?
struct OpRule {
    bool ok = true;
    std::string code = "E000";
    std::string problem;
    std::string why;
    std::string fix;
};
OpRule checkBinaryOp(const std::string& op, Type t);
OpRule checkUnaryOp(const std::string& op, Type t);

// ---- conversions (main-spec §5.2, §5.3) ---------------------------------------------

// How an assignment, argument or `return` treats a value of type `from` where
// `to` is expected.
enum class Flow {
    Same,     // nothing to do (Lean elaborates literals, `Int`/`Nat` coerce)
    Convert,  // an implicit conversion is inserted: number → bool (D10), literal → char
    Refuse,   // needs an explicit cast
};
Flow flowKind(Type from, Type to);

// The Lean expression converting `x` (already atomic: a name, a literal or
// parenthesised) from `from` to `to`. Empty on failure, with `why`/`fix` set.
// Used for explicit casts `(T)e` and for the implicit conversions above.
std::string convert(Type from, Type to, const std::string& x, std::string* why, std::string* fix);

// ---- tests -----------------------------------------------------------------------

// A value of this type used as a condition (C: "non-zero is true", §5.2).
bool hasTruth(Type t);
std::string truthTest(Type t, const std::string& x);

// The Lean spelling of a comparison operator between values of this type. In a
// test it is a `Prop` (`=`, `≠`, `<`); `Float` equality is `==`/`!=` because
// Lean has no decidable `=` on floats.
std::string cmpSpelling(Type t, const std::string& cOp);
// True when that spelling is already a `Bool` (so a Bool-valued comparison
// needs no `decide`).
bool cmpIsBool(Type t, const std::string& cOp);
