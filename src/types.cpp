// types.cpp — implementation of types.h. The only file that names C types,
// Lean types, and the rules for mixing and converting them.

#include "types.h"

#include <map>
#include <set>
#include <utility>

std::string leanName(Type t);  // defined below; the helpers in the anonymous namespace use it

namespace {

// ---- the spelling table -------------------------------------------------------

// Words that make up the types clean translates (main-spec §4.1).
const std::set<std::string> kSupported = {
    "int", "unsigned", "signed", "short", "long", "size_t",           // Int / Nat family
    "char", "bool", "_Bool", "float", "double",                      // Char, Bool, Float32, Float
    "int8_t", "int16_t", "int32_t", "int64_t",                       // Int8 … Int64
    "uint8_t", "uint16_t", "uint32_t", "uint64_t",                   // UInt8 … UInt64
    "bv", "_BitVec",                                                 // BitVec N (parser reads the `(N)`)
    "void",                                                          // Unit (a return type only)
    "const", "restrict",                                             // qualifiers: ignored (§4.7)
};

// C type and storage words that exist in the main spec but are not built
// yet: a pointed refusal beats "expected a declaration".
const std::set<std::string> kRefused = {
    "struct", "union", "enum", "typedef", "static", "extern",
    "inline", "volatile", "register", "_Atomic",
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
    case Type::Kind::Char:
    case Type::Kind::String:
    case Type::Kind::Array:
    case Type::Kind::Unit: return false;
    default: return true;
    }
}

// Rank for mixing (see `unify`); -1 = takes no part.
int rank(Type t) {
    switch (t.kind) {
    case Type::Kind::Nat:     return 1;
    case Type::Kind::Int:     return 2;
    case Type::Kind::SInt:
    case Type::Kind::UInt:    return 3;
    case Type::Kind::BitVec:  return 4;
    case Type::Kind::Float32: return 5;
    case Type::Kind::Float64: return 6;
    default:                  return -1;
    }
}

std::string wrapName(Type t) {  // a type name as an argument: `Array (BitVec 8)`
    std::string n = leanName(t);
    return n.find(' ') == std::string::npos ? n : "(" + n + ")";
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
bool isArray(Type t) { return t.kind == Type::Kind::Array; }
bool isString(Type t) { return t.kind == Type::Kind::String; }

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
    case Type::Kind::String:       return "String";
    case Type::Kind::Array:        return "Array " + wrapName(t.elem());
    case Type::Kind::Unit:         return "Unit";
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
    case Type::Kind::String:       return "char *";
    case Type::Kind::Array:        return cName(t.elem()) + "[]";
    case Type::Kind::Unit:         return "void";
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
    return "`int`, `unsigned`, `uint8_t`/`int32_t`…, `bv(N)`, `bool`, `char`, `float`, `double`, `char *` or an array";
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

TypeResolution resolveTypeWords(const std::vector<std::string>& allWords) {
    // `const` and `restrict` are ignored (§4.7); what is left spells the type.
    std::vector<std::string> words;
    for (const std::string& w : allWords)
        if (w != "const" && w != "restrict") words.push_back(w);
    if (words.empty())
        return bad("a type is missing after `" + joined(allWords) + "`", "a qualifier is not a type.",
                   "write the type: " + supportedTypesHint() + ".");
    const std::string spelled = joined(words);
    if (words.size() == 1 && words[0] == "void") return good(Type::of(Type::Kind::Unit));
    for (const std::string& w : words)
        if (w == "void")
            return bad("`" + spelled + "` is not a valid C type", "`void` stands alone.", "fix the spelling.");
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

TypeResolution applyDeclarator(Type base, int star, int brackets, bool isParam) {
    const auto e101 = [&](const std::string& problem, const std::string& why, const std::string& fix) {
        TypeResolution r = bad(problem, why, fix);
        r.code = "E101";
        return r;
    };
    const bool isChar = base.kind == Type::Kind::Char;
    if (star == 0 && brackets == 0) return good(base);
    if (base.kind == Type::Kind::Unit)
        return e101("`void *` is a pointer to nothing in particular", "main-spec §4.6 accepts only array "
                    "parameters and `char *`.", "use an array parameter or a return value.");
    if (star > 1 || (star && brackets))
        return e101("pointer-to-pointer or array-of-pointers declarator",
                    "main-spec §4.6 accepts only `T *a` as an array parameter and `char *` as a string.",
                    "use `T a[]` for arrays; arrays of strings are not translated yet.");
    if (isChar) {
        if (star == 1 || brackets == 1) return good(Type::of(Type::Kind::String));
        return bad("an array of strings is not translated yet", "`char a[M][N]` would be an `Array String`.",
                   "use `char *` for one string.");
    }
    if (star == 1) {
        if (!isParam)
            return e101("pointer `" + cName(base) + " *` outside a parameter list",
                        "main-spec §4.6 accepts a pointer only as an array parameter (`T *a, size_t n`) or `char *`.",
                        "declare an array (`" + cName(base) + " a[N]`) instead.");
        return good(Type::arrayOf(base));
    }
    Type t = base;
    for (int k = 0; k < brackets; ++k) t = Type::arrayOf(t);
    return good(t);
}

// ---- mixing -----------------------------------------------------------------------

Unified unify(Type a, Type b, bool comparison) {
    Unified u;
    u.ok = true;
    using K = Type::Kind;
    // `char` is its code in arithmetic (§4.2); only two chars compared stay `Char`.
    if (a.kind == K::Char && b.kind == K::Char && comparison) {
        u.type = a;
        return u;
    }
    if (a.kind == K::Char) a = Type::of(K::Nat);
    if (b.kind == K::Char) b = Type::of(K::Nat);

    if (a == b) {
        u.type = a;
        return u;
    }
    const auto refuse = [&](const std::string& code, const std::string& problem, const std::string& why,
                            const std::string& fix) {
        u.ok = false;
        u.code = code;
        u.problem = problem;
        u.why = why;
        u.fix = fix;
        return u;
    };
    const bool aLit = isLiteral(a), bLit = isLiteral(b);
    if (aLit && bLit) {  // `1 + 1.5`: the floating literal wins
        u.type = Type::of(K::FloatLiteral);
        return u;
    }
    if (aLit || bLit) {
        const Type lit = aLit ? a : b;
        const Type other = aLit ? b : a;
        if (isNumeric(other)) {
            if (lit.kind == K::Literal) {
                u.type = other;
                return u;
            }
            // a floating literal: floats keep their width, anything else becomes `double`
            u.type = other.kind == K::Float32 ? other : Type::of(K::Float64);
            return u;
        }
    }
    const int ra = rank(a), rb = rank(b);
    if (ra < 0 || rb < 0)
        return refuse("E000", "`" + cName(a) + "` and `" + cName(b) + "` are mixed in one operation",
                      "main-spec §5.3 has no conversion between them, and clean does not pick one (D5).",
                      "compare or convert explicitly, e.g. `x != 0`.");
    if (a.kind == K::BitVec && b.kind == K::BitVec)
        return refuse("E301", "bit-width mismatch: `" + cName(a) + "` and `" + cName(b) + "` in one operation",
                      "a bit-vector operation needs equal widths; Lean rejects the rest.",
                      "make the widths equal (the `bv_zext`/`bv_sext`/`bv_ext` helpers of main-spec §5.4 "
                      "are not built yet).");
    if (ra == 3 && rb == 3) {  // two fixed widths: wider wins; same width: the unsigned one
        if (a.width != b.width) u.type = a.width > b.width ? a : b;
        else u.type = a.kind == K::UInt ? a : b;
        return u;
    }
    u.type = ra >= rb ? a : b;
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

    if (t.kind == Type::Kind::String || t.kind == Type::Kind::Array || t.kind == Type::Kind::Unit)
        return refuse("E000", what + " is not translated",
                      "a `" + cName(t) + "` is not a number in Lean.",
                      t.kind == Type::Kind::String ? "string helpers (`strcat`, …) are not built yet."
                                                   : "index it (`a[i]`) to get an element.");
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
    case Type::Kind::String:
    case Type::Kind::Array:
    case Type::Kind::Unit: return r;  // refused above
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
        case Type::Kind::String:
        case Type::Kind::Array:
        case Type::Kind::Unit:
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
    case Type::Kind::String:
    case Type::Kind::Array:
    case Type::Kind::Unit:
        return refuse("E000", what + " is not translated", "it is not a number in Lean.",
                      "convert to a number first.");
    }
    return r;
}

// ---- conversions ---------------------------------------------------------------------

Flow flowKind(Type from, Type to) {
    if (from == to) return Flow::Same;
    if (from.kind == Type::Kind::Literal) {
        if (to.kind == Type::Kind::Bool || to.kind == Type::Kind::Char) return Flow::Convert;
        return isNumeric(to) ? Flow::Same : Flow::Refuse;  // Lean elaborates the numeral at `to`
    }
    if (from.kind == Type::Kind::FloatLiteral) return isFloat(to) ? Flow::Same : Flow::Refuse;
    // Decision D19: any other implicit conversion is inserted when main-spec §5.3 has a row for it.
    return convert(from, to, "x", nullptr, nullptr).empty() ? Flow::Refuse : Flow::Convert;
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

    if (from.kind == K::Array)
        return fail(kNoRow, "index the array (`a[i]`) to get an element.");
    if (from.kind == K::String)
        return fail(kNoRow, "index the string (`s[i]`) to get a character, or compare it with `==`.");
    if (from.kind == K::Unit)
        return fail(kNoRow, "a `void` call has no value; call it as a statement.");

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
    case K::String:
    case K::Array:
    case K::Unit: return fail(kNoRow, "there is no conversion to this type.");
    }
    return fail(kNoRow, "cast through an integer type first.");
}

// ---- tests ------------------------------------------------------------------------------

bool hasTruth(Type t) {
    return t.kind != Type::Kind::Char && t.kind != Type::Kind::Array && t.kind != Type::Kind::Unit;
}

std::string truthTest(Type t, const std::string& x) {
    switch (t.kind) {
    case Type::Kind::Bool:         return x;
    case Type::Kind::BitVec:       return x + " ≠ 0#" + std::to_string(t.width);
    case Type::Kind::Float32:
    case Type::Kind::Float64:
    case Type::Kind::FloatLiteral: return x + " != 0.0";
    case Type::Kind::String:       return x + " != \"\"";  // §5.2
    case Type::Kind::Char:
    case Type::Kind::Array:
    case Type::Kind::Unit:         return "";
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

OpRule checkCompare(const std::string& op, Type t) {
    OpRule r;
    const bool eq = op == "==" || op == "!=";
    const auto refuse = [&](const std::string& problem, const std::string& why, const std::string& fix) {
        r.ok = false;
        r.problem = problem;
        r.why = why;
        r.fix = fix;
        return r;
    };
    switch (t.kind) {
    case Type::Kind::Bool:
        if (!eq) return refuse("`" + op + "` on `bool`", "Lean's `Bool` has no order; only `==` and `!=`.",
                               "compare the underlying numbers instead.");
        return r;
    case Type::Kind::String:
        if (!eq) return refuse("`" + op + "` on strings", "ordering strings is `strcmp` in C; the string "
                               "helpers are not built yet.", "compare with `==` / `!=`.");
        return r;
    case Type::Kind::Array:
        return refuse("comparison of arrays", "in C it would compare addresses, which Lean arrays do not have.",
                      "compare the elements.");
    case Type::Kind::Unit:
        return refuse("comparison of `void`", "there is no value.", "remove it.");
    default: return r;
    }
}

std::string zeroValue(Type t) {
    switch (t.kind) {
    case Type::Kind::Literal:
    case Type::Kind::Int:
    case Type::Kind::Nat:
    case Type::Kind::SInt:
    case Type::Kind::UInt:         return "0";
    case Type::Kind::BitVec:       return "0#" + std::to_string(t.width);
    case Type::Kind::FloatLiteral:
    case Type::Kind::Float32:
    case Type::Kind::Float64:      return "0.0";
    case Type::Kind::Bool:         return "false";
    case Type::Kind::Char:         return "'\\x00'";
    case Type::Kind::String:       return "\"\"";
    case Type::Kind::Array:        return "#[]";
    case Type::Kind::Unit:         return "()";
    }
    return "";
}
