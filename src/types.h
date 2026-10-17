// types.h — everything `clean` knows about C data types, in one place.
//
// Layer: shared knowledge, no IO. The parser asks it "what type is spelled
// by these C words?", the checker asks it "what type does `a + b` have?",
// the emitter asks it "how is this type written in Lean?". No other file
// may mention a C type name or a Lean type name.
//
// To add a datatype (see notes/adding-a-type.md for the full recipe):
//   1. a `Kind` below;
//   2. its C spelling in types.cpp (`resolveTypeWords`, or a word in `kSupported`);
//   3. a `case` in `leanName` and `truthSuffix`, and a rule in `commonType`
//      if it mixes with other types.
// The compiler's -Wswitch then points at every switch that needs a case.

#pragma once

#include <string>
#include <vector>

struct Type {
    enum class Kind {
        Literal,  // an integer literal that Lean has not pinned to a type yet (`42`)
        Int,      // C `int`, `signed`, `short`, `long`, `long long`  → Lean `Int`
        Nat,      // C `unsigned …`, `size_t`                         → Lean `Nat`
    };
    Kind kind = Kind::Int;

    bool operator==(const Type& o) const { return kind == o.kind; }
    bool operator!=(const Type& o) const { return !(*this == o); }
};

// The Lean spelling of a type: `Int`, `Nat`, …
std::string leanName(Type t);

// What turns a value of this type into a Lean test (C: "non-zero is true"):
// appended after the value, `x` + ` ≠ 0`.
std::string truthSuffix(Type t);

// The type of `a + b`, `a * b`, … given the operand types. Mixed types follow
// Lean's own coercion (D4): `Int` and `Nat` together give `Int`.
Type commonType(Type a, Type b);

// ---- reading a type out of C source -----------------------------------------

// How the parser sees one word of a type specifier.
enum class TypeWord {
    NotAType,   // an ordinary identifier
    Supported,  // part of a type clean translates (`int`, `unsigned`, `long`, …)
    Refused,    // a C type / storage word clean knows but does not translate yet
};
TypeWord classifyTypeWord(const std::string& word);

// The words of ONE type specifier, in source order (`{"unsigned","long","long"}`).
// On success `ok` is true and `type` is set; otherwise `problem` says why the
// combination is refused and `fix` what to write instead.
struct TypeResolution {
    bool ok = false;
    Type type;
    std::string problem;
    std::string why;
    std::string fix;
};
TypeResolution resolveTypeWords(const std::vector<std::string>& words);

// "`int` or `unsigned`"-style hint for messages, so they never go stale.
std::string supportedTypesHint();
