// types.cpp — implementation of types.h. The only file that names C types,
// Lean types, and the rules for mixing and converting them.

#include "types.h"

#include <map>
#include <set>
#include <utility>

namespace {

// ---- the spelling table -------------------------------------------------------

// Words that make up the types clean translates (main-spec §4.1).
const std::set<std::string> kSupported = {
    "int", "unsigned", "signed", "short", "long", "size_t",           // Int / Nat family
    "char", "bool", "_Bool", "float", "double",                      // Char, Bool, Float32, Float
    "int8_t", "int16_t", "int32_t", "int64_t",                       // Int8 … Int64
    "uint8_t", "uint16_t", "uint32_t", "uint64_t",                   // UInt8 … UInt64
    "bv", "_BitVec",                                                 // BitVec N (parser reads the `(N)`)
};

// C type and storage words that exist in the main spec but are not built
// yet: a pointed refusal beats "expected a declaration".
const std::set<std::string> kRefused = {
    "void", "struct", "union", "enum", "typedef", "const", "static", "extern",
    "inline", "restrict", "volatile", "register", "_Atomic",
    "__int128", "int128_t", "uint128_t", "_Float16",
};

// Words refused for good, with the reason (main-spec §4.1 ⛔ rows).
const std::map<std::string, std::string> kNever = {
    {"__int128", "Lean has no 128-bit fixed integer; use `bv(128)`."},
    {"int128_t", "Lean has no 128-bit fixed integer; use `bv(128)`."},
    {"uint128_t", "Lean has no 128-bit fixed integer; use `bv(128)`."},
    {"_Float16", "Lean has no 16-bit float."},
};

std::string joined(const std::vector<std::string>& words) {
    std::string s;
    for (const std::string& w : words) s += (s.empty() ? "" : " ") + w;
    return s;
}

TypeResolution bad(std::string problem, std::string why, std::string fix) {
    TypeResolution r;
    r.problem = std::move(problem);
    r.why = std::move(why);
    r.fix = std::move(fix);
    return r;
}

TypeResolution good(Type t) {
    TypeResolution r;
    r.ok = true;
    r.type = t;
    return r;
}

// `int8_t` → (true, 8); `uint32_t` → (false, 32). False return: not a fixed-width word.
bool fixedWidth(const std::string& w, bool& isSigned, int& bits) {
    static const std::map<std::string, std::pair<bool, int>> t = {
        {"int8_t", {true, 8}},   {"int16_t", {true, 16}},   {"int32_t", {true, 32}},
        {"int64_t", {true, 64}}, {"uint8_t", {false, 8}},   {"uint16_t", {false, 16}},
        {"uint32_t", {false, 32}}, {"uint64_t", {false, 64}},
    };
    auto it = t.find(w);
    if (it == t.end()) return false;
    isSigned = it->second.first;
    bits = it->second.second;
    return true;
}

// 2^bits as a decimal string (bits ∈ {8, 16, 32, 64}).
std::string pow2(int bits) {
    switch (bits) {
    case 8:  return "256";
    case 16: return "65536";
    case 32: return "4294967296";
    default: return "18446744073709551616";
    }
}

bool isNumeric(Type t) {
    switch (t.kind) {
    case Type::Kind::Bool:
    case Type::Kind::Char: return false;
    default: return true;
    }
}

bool isFloatLike(Type t) {  // Float32, Float64, FloatLiteral
    return t.kind == Type::Kind::Float32 || t.kind == Type::Kind::Float64 ||
           t.kind == Type::Kind::FloatLiteral;
}

}  // namespace

// ---- names ----------------------------------------------------------------------

bool isLiteral(Type t) {
    return t.kind == Type::Kind::Literal || t.kind == Type::Kind::FloatLiteral;
}

bool isFloat(Type t) { return t.kind == Type::Kind::Float32 || t.kind == Type::Kind::Float64; }

std::string leanName(Type t) {
    switch (t.kind) {
    case Type::Kind::Literal:      return "Int";  // a literal with no context defaults to Int
    case Type::Kind::FloatLiteral: return "Float";
    case Type::Kind::Int:          return "Int";
    case Type::Kind::Nat:          return "Nat";
    case Type::Kind::SInt:         return "Int" + std::to_string(t.width);
    case Type::Kind::UInt:         return "UInt" + std::to_string(t.width);
    case Type::Kind::BitVec:       return "BitVec " + std::to_string(t.width);
    case Type::Kind::Float32:      return "Float32";
    case Type::Kind::Float64:      return "Float";
    case Type::Kind::Bool:         return "Bool";
    case Type::Kind::Char:         return "Char";
    }
    return "";
}

std::string cName(Type t) {
    switch (t.kind) {
    case Type::Kind::Literal:      return "int";
    case Type::Kind::FloatLiteral: return "double";
    case Type::Kind::Int:          return "int";
    case Type::Kind::Nat:          return "unsigned";
    case Type::Kind::SInt:         return "int" + std::to_string(t.width) + "_t";
    case Type::Kind::UInt:         return "uint" + std::to_string(t.width) + "_t";
    case Type::Kind::BitVec:       return "bv(" + std::to_string(t.width) + ")";
    case Type::Kind::Float32:      return "float";
    case Type::Kind::Float64:      return "double";
    case Type::Kind::Bool:         return "bool";
    case Type::Kind::Char:         return "char";
    }
    return "";
}

// ---- reading a type out of C source -----------------------------------------------

TypeWord classifyTypeWord(const std::string& word) {
    if (kSupported.count(word)) return TypeWord::Supported;
    if (kRefused.count(word)) return TypeWord::Refused;
    return TypeWord::NotAType;
}

bool isBitVecWord(const std::string& word) { return word == "bv" || word == "_BitVec"; }

std::string supportedTypesHint() {
    return "`int`, `unsigned`, `uint8_t`/`int32_t`…, `bv(N)`, `bool`, `char`, `float` or `double`";
}

TypeResolution makeBitVec(long long width) {
    if (width < 1)
        return [] {
            TypeResolution r = bad("`bv(0)` is not a type",
                                   "a bit-vector needs at least one bit.", "write `bv(1)` or wider.");
            r.code = "E302";
            return r;
        }();
    return good(Type::of(Type::Kind::BitVec, (int)width));
}

TypeResolution resolveTypeWords(const std::vector<std::string>& words) {
    const std::string spelled = joined(words);
    for (const std::string& w : words) {
        auto never = kNever.find(w);
        if (never != kNever.end())
            return bad("type `" + spelled + "` has no Lean counterpart", never->second,
                       "use " + supportedTypesHint() + ".");
        if (classifyTypeWord(w) == TypeWord::Refused)
            return bad("type `" + spelled + "` is not translated in this slice",
                       "`" + w + "` exists in the main spec but is not translated yet; the type "
                       "table grows slice by slice.",
                       "use " + supportedTypesHint() + " for now.");
    }

    const auto invalid = [&] {
        return bad("`" + spelled + "` is not a valid C type",
                   "C rejects this combination of type words.", "fix the spelling.");
    };

    // Words that stand for a whole type on their own.
    int nBase = 0;
    std::string base;
    for (const std::string& w : words) {
        bool s;
        int b;
        if (w == "bool" || w == "_Bool" || w == "float" || w == "double" || w == "size_t" ||
            fixedWidth(w, s, b)) {
            ++nBase;
            base = w;
        }
    }
    if (nBase > 1) return invalid();
    if (nBase == 1) {
        if (base == "double") {  // `double`, or `long double` (precision lint W003)
            if (words.size() == 1) return good(Type::of(Type::Kind::Float64));
            if (words.size() == 2 && (words[0] == "long" || words[1] == "long"))
                return good(Type::of(Type::Kind::Float64));
            return invalid();
        }
        if (words.size() != 1) return invalid();
        bool isSigned;
        int bits;
        if (fixedWidth(base, isSigned, bits))
            return good(Type::of(isSigned ? Type::Kind::SInt : Type::Kind::UInt, bits));
        if (base == "float") return good(Type::of(Type::Kind::Float32));
        if (base == "size_t") return good(Type::of(Type::Kind::Nat));
        return good(Type::of(Type::Kind::Bool));  // bool, _Bool
    }

    // `char`, `unsigned char`.
    int nChar = 0, nUnsigned = 0, nSigned = 0, nShort = 0, nLong = 0, nInt = 0;
    for (const std::string& w : words) {
        if (w == "char") ++nChar;
        else if (w == "unsigned") ++nUnsigned;
        else if (w == "signed") ++nSigned;
        else if (w == "short") ++nShort;
        else if (w == "long") ++nLong;
        else if (w == "int") ++nInt;
    }
    if (nChar) {
        if (nChar > 1 || nShort || nLong || nInt || (nUnsigned && nSigned)) return invalid();
        if (nUnsigned) return good(Type::of(Type::Kind::UInt, 8));
        if (nSigned)
            return bad("type `signed char` is not translated",
                       "the main spec maps `char` to `Char` and `unsigned char` to `UInt8`, "
                       "but has no row for `signed char`.",
                       "write `int8_t`.");
        return good(Type::of(Type::Kind::Char));
    }

    // The integer family: C spells one type with several words in any order.
    if (nUnsigned > 1 || nSigned > 1 || nShort > 1 || nInt > 1 || nLong > 2 ||
        (nUnsigned && nSigned) || (nShort && nLong))
        return invalid();
    if (nUnsigned) return good(Type::of(Type::Kind::Nat));
    return good(Type::of(Type::Kind::Int));
}

// ---- mixing -----------------------------------------------------------------------

Unified unify(Type a, Type b) {
    Unified u;
    u.ok = true;
    if (a == b) {
        u.type = a;
        return u;
    }
    const bool aLit = isLiteral(a), bLit = isLiteral(b);
    if (aLit || bLit) {
        const Type lit = aLit ? a : b;
        const Type other = aLit ? b : a;
        if (aLit && bLit) {  // `1 + 1.5`: the floating literal wins
            u.type = Type::of(Type::Kind::FloatLiteral);
            return u;
        }
        if (lit.kind == Type::Kind::Literal && isNumeric(other)) {
            u.type = other;
            return u;
        }
        if (lit.kind == Type::Kind::FloatLiteral && isFloat(other)) {
            u.type = other;
            return u;
        }
    }
    if ((a.kind == Type::Kind::Int && b.kind == Type::Kind::Nat) ||
        (a.kind == Type::Kind::Nat && b.kind == Type::Kind::Int)) {
        u.type = Type::of(Type::Kind::Int);  // Lean coerces Nat up to Int (D4)
        return u;
    }
    u.ok = false;
    if (a.kind == Type::Kind::BitVec && b.kind == Type::Kind::BitVec) {
        u.code = "E301";
        u.problem = "bit-width mismatch: `" + cName(a) + "` and `" + cName(b) + "` in one operation";
        u.why = "a bit-vector operation needs equal widths; Lean rejects the rest.";
        u.fix = "make the widths equal (the `bv_zext`/`bv_sext`/`bv_ext` helpers of main-spec §5.4 "
                "are not built yet).";
        return u;
    }
    u.problem = "`" + cName(a) + "` and `" + cName(b) + "` are mixed in one operation";
    u.why = "Lean has no implicit conversion between them, and clean does not pick one (D5).";
    u.fix = "convert one side explicitly, e.g. `(" + cName(a) + ")x`.";
    return u;
}

OpRule checkBinaryOp(const std::string& op, Type t) {
    OpRule r;
    const bool bitwise = op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>";
    const auto refuse = [&](const char* code, const std::string& problem, const std::string& why,
                            const std::string& fix) {
        r.ok = false;
        r.code = code;
        r.problem = problem;
        r.why = why;
        r.fix = fix;
        return r;
    };
    const std::string what = "`" + op + "` on `" + cName(t) + "`";

    if (!bitwise) {
        if (t.kind == Type::Kind::Bool)
            return refuse("E000", what + " is not translated",
                          "a `bool` is not a number in Lean.",
                          "use `&&`, `||`, `!` on bools, or compare first.");
        if (t.kind == Type::Kind::Char)
            return refuse("E000", what + " is not translated",
                          "Lean's `Char` has no arithmetic (main-spec §4.2 converts through `Nat`).",
                          "convert first: `(unsigned)c`, and back with `(char)n`.");
        if (op == "%" && isFloatLike(t))
            return refuse("E000", what + " is not translated", "Lean's floats have no `%` operator.",
                          "avoid `%` on floating values.");
        return r;
    }
    switch (t.kind) {
    case Type::Kind::Literal:
    case Type::Kind::Nat:
    case Type::Kind::SInt:
    case Type::Kind::UInt:
    case Type::Kind::BitVec: return r;
    case Type::Kind::Int:
        return refuse("E303", "bitwise `" + op + "` on `int`",
                      "Lean's `Int` has no bitwise operators.", "use `uintN_t` or `bv(N)`.");
    case Type::Kind::Char:
        return refuse("H004", "bitwise `" + op + "` on `char`", "Lean's `Char` has no bitwise operators.",
                      "use `uint8_t`.");
    case Type::Kind::Bool:
        return refuse("E000", what + " is not translated", "a `bool` is not a number in Lean.",
                      "use `&&`, `||`, `!` on bools.");
    case Type::Kind::FloatLiteral:
    case Type::Kind::Float32:
    case Type::Kind::Float64:
        return refuse("E000", what + " is not translated", "C itself has no bitwise operators on floats.",
                      "convert to an integer type first.");
    }
    return r;
}

OpRule checkUnaryOp(const std::string& op, Type t) {
    OpRule r;
    const auto refuse = [&](const char* code, const std::string& problem, const std::string& why,
                            const std::string& fix) {
        r.ok = false;
        r.code = code;
        r.problem = problem;
        r.why = why;
        r.fix = fix;
        return r;
    };
    const std::string what = "unary `" + op + "` on `" + cName(t) + "`";
    if (op == "-") {
        switch (t.kind) {
        case Type::Kind::Literal:
        case Type::Kind::FloatLiteral:
        case Type::Kind::Int:
        case Type::Kind::SInt:
        case Type::Kind::BitVec:
        case Type::Kind::Float32:
        case Type::Kind::Float64: return r;
        case Type::Kind::Nat:
        case Type::Kind::UInt:
            return refuse("E000", what + " is not translated",
                          "Lean has no negation on unsigned types (`Nat` has no `Neg`).",
                          "write `0 - x`.");
        case Type::Kind::Bool:
        case Type::Kind::Char:
            return refuse("E000", what + " is not translated", "it is not a number in Lean.",
                          "convert to a number first.");
        }
        return r;
    }
    // `~`
    switch (t.kind) {
    case Type::Kind::Literal:
    case Type::Kind::SInt:
    case Type::Kind::UInt:
    case Type::Kind::BitVec: return r;
    case Type::Kind::Int:
        return refuse("E303", "bitwise `~` on `int`", "Lean's `Int` has no bitwise operators.",
                      "use `uintN_t` or `bv(N)`.");
    case Type::Kind::Char:
        return refuse("H004", "bitwise `~` on `char`", "Lean's `Char` has no bitwise operators.",
                      "use `uint8_t`.");
    case Type::Kind::Nat:
        return refuse("E000", what + " is not translated",
                      "a `Nat` has no complement (it has no fixed width).", "use `uintN_t` or `bv(N)`.");
    case Type::Kind::Bool:
        return refuse("E000", what + " is not translated", "a `bool` is not a number in Lean.",
                      "use `!`.");
    case Type::Kind::FloatLiteral:
    case Type::Kind::Float32:
    case Type::Kind::Float64:
        return refuse("E000", what + " is not translated", "C itself has no `~` on floats.",
                      "convert to an integer type first.");
    }
    return r;
}

// ---- conversions ---------------------------------------------------------------------

Flow flowKind(Type from, Type to) {
    if (from == to) return Flow::Same;
    if (from.kind == Type::Kind::Literal) {
        if (to.kind == Type::Kind::Bool || to.kind == Type::Kind::Char) return Flow::Convert;
        return isNumeric(to) ? Flow::Same : Flow::Refuse;
    }
    if (from.kind == Type::Kind::FloatLiteral) return isFloat(to) ? Flow::Same : Flow::Refuse;
    if (from.kind == Type::Kind::Nat && to.kind == Type::Kind::Int) return Flow::Same;  // Lean's ↑
    if (to.kind == Type::Kind::Bool && isNumeric(from)) return Flow::Convert;           // D10
    return Flow::Refuse;
}

std::string convert(Type from, Type to, const std::string& x, std::string* why, std::string* fix) {
    using K = Type::Kind;
    if (from == to) return x;

    const auto fail = [&](const std::string& w, const std::string& f) {
        if (why) *why = w;
        if (fix) *fix = f;
        return std::string();
    };

    // `x` as a Nat / Int Lean term, or "" when the main spec has no such conversion.
    const auto asNat = [&]() -> std::string {
        switch (from.kind) {
        case K::Nat:  return x;
        case K::Int:  // clamps negatives to 0 (lint W009)
        case K::UInt:
        case K::BitVec:
        case K::Char: return x + ".toNat";
        case K::SInt: return x + ".toInt.toNat";
        default:      return "";
        }
    };
    const auto asInt = [&]() -> std::string {
        switch (from.kind) {
        case K::Int:  return x;
        case K::Nat:  return "(↑" + x + " : Int)";
        case K::UInt:
        case K::Char: return "(↑" + x + ".toNat : Int)";
        case K::SInt: return x + ".toInt";
        default:      return "";  // BitVec is signless: no signed reading
        }
    };
    const std::string kNoRow = "main-spec §5.3 has no conversion from `" + cName(from) + "` to `" +
                               cName(to) + "`.";

    // A literal takes its type from where it is used: no conversion, only an ascription.
    if (from.kind == K::Literal || from.kind == K::FloatLiteral) {
        if (to.kind == K::Bool) {
            if (from.kind == K::FloatLiteral) return "(" + x + " != 0.0)";
            if (x == "0") return "false";
            if (x == "1") return "true";
            return "decide (" + x + " ≠ 0)";
        }
        if (to.kind == K::Char) {
            if (from.kind == K::Literal) return "Char.ofNat " + x;  // W007
            return fail(kNoRow, "write an integer literal.");
        }
        if (from.kind == K::FloatLiteral && !isFloat(to))
            return fail("C would truncate the floating literal; clean does not guess.",
                        "write the integer value.");
        return "(" + x + " : " + leanName(to) + ")";
    }

    switch (to.kind) {
    case K::Literal:
    case K::FloatLiteral: return fail(kNoRow, "cast to a concrete type.");
    case K::Int: {
        std::string s = asInt();
        return s.empty() ? fail(kNoRow, "cast through `unsigned` or an `intN_t` type.") : s;
    }
    case K::Nat: {
        std::string s = asNat();
        return s.empty() ? fail(kNoRow, "cast through an integer type first.") : s;
    }
    case K::UInt: {
        const std::string ctor = "UInt" + std::to_string(to.width) + ".ofNat ";
        if (isFloat(from)) {
            std::string f = "Float.toUInt" + std::to_string(to.width);
            return f + " " + (from.kind == K::Float32 ? "(Float32.toFloat " + x + ")" : x);
        }
        if (from.kind == K::Int) return ctor + "((" + x + " % " + pow2(to.width) + ").toNat)";
        if (from.kind == K::SInt) return ctor + "((" + x + ".toInt % " + pow2(to.width) + ").toNat)";
        std::string s = asNat();
        return s.empty() ? fail(kNoRow, "cast through an integer type first.") : ctor + s;
    }
    case K::SInt: {
        const std::string w = std::to_string(to.width);
        if (isFloat(from)) {
            std::string f = "Float.toInt" + w;
            return f + " " + (from.kind == K::Float32 ? "(Float32.toFloat " + x + ")" : x);
        }
        if (from.kind == K::Int) return "Int" + w + ".ofInt " + x;
        if (from.kind == K::SInt) return "Int" + w + ".ofInt " + x + ".toInt";
        std::string s = asNat();
        if (from.kind == K::Bool || s.empty()) return fail(kNoRow, "cast through an integer type first.");
        return "Int" + w + ".ofNat " + s;
    }
    case K::BitVec: {
        const std::string w = std::to_string(to.width);
        if (from.kind == K::BitVec)
            return fail("a different width needs an explicit extension or truncation "
                        "(E301); the §5.4 helpers are not built yet.",
                        "keep both sides at one width.");
        if (from.kind == K::Nat) return "BitVec.ofNat " + w + " " + x;
        if (from.kind == K::Int) return "BitVec.ofInt " + w + " " + x;
        if (from.kind == K::SInt) return "BitVec.ofInt " + w + " " + x + ".toInt";
        std::string s = asNat();
        return s.empty() || isFloat(from) ? fail(kNoRow, "cast through an integer type first.")
                                          : "BitVec.ofNat " + w + " " + s;
    }
    case K::Float64: {
        if (from.kind == K::Float32) return "Float32.toFloat " + x;
        if (from.kind == K::Nat) return "Float.ofNat " + x;
        if (from.kind == K::Int) return "Float.ofInt " + x;
        if (from.kind == K::SInt) return "Float.ofInt " + x + ".toInt";
        std::string s = asNat();
        return s.empty() ? fail(kNoRow, "cast through an integer type first.") : "Float.ofNat " + s;
    }
    case K::Float32: {
        if (from.kind == K::Float64) return "Float.toFloat32 " + x;
        std::string wide = convert(from, Type::of(K::Float64), x, why, fix);
        return wide.empty() ? wide : "Float.toFloat32 (" + wide + ")";
    }
    case K::Bool:
        if (!isNumeric(from)) return fail(kNoRow, "compare explicitly.");
        return isFloat(from) ? "(" + x + " != 0.0)" : "decide (" + truthTest(from, x) + ")";
    case K::Char: {
        if (from.kind == K::Bool || from.kind == K::SInt || isFloat(from))
            return fail(kNoRow, "cast through `unsigned` first.");
        std::string s = asNat();
        return s.empty() ? fail(kNoRow, "cast through `unsigned` first.") : "Char.ofNat " + s;  // W007
    }
    }
    return fail(kNoRow, "cast through an integer type first.");
}

// ---- tests ------------------------------------------------------------------------------

bool hasTruth(Type t) { return t.kind != Type::Kind::Char; }

std::string truthTest(Type t, const std::string& x) {
    switch (t.kind) {
    case Type::Kind::Bool:         return x;
    case Type::Kind::BitVec:       return x + " ≠ 0#" + std::to_string(t.width);
    case Type::Kind::Float32:
    case Type::Kind::Float64:
    case Type::Kind::FloatLiteral: return x + " != 0.0";
    case Type::Kind::Char:         return "";
    case Type::Kind::Literal:
    case Type::Kind::Int:
    case Type::Kind::Nat:
    case Type::Kind::SInt:
    case Type::Kind::UInt:         return x + " ≠ 0";
    }
    return "";
}

std::string cmpSpelling(Type t, const std::string& cOp) {
    if (cOp == "==") return isFloatLike(t) ? "==" : "=";
    if (cOp == "!=") return isFloatLike(t) ? "!=" : "≠";
    return cOp;  // < <= > >= read the same
}

bool cmpIsBool(Type t, const std::string& cOp) {
    return isFloatLike(t) && (cOp == "==" || cOp == "!=");
}
