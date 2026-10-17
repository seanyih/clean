// types.cpp — implementation of types.h. The only file that names C types.

#include "types.h"

#include <set>

namespace {

// Words that make up the types clean translates today (main-spec §4.1 rows
// built so far: the `int` family → Int, the `unsigned` family → Nat).
const std::set<std::string> kSupported = {"int", "unsigned", "signed", "short", "long", "size_t"};

// C type and storage words that exist in the main spec but are not built
// yet. Better a pointed refusal than "expected a declaration".
const std::set<std::string> kRefused = {
    "void", "char", "float", "double", "bool", "_Bool", "struct", "union", "enum",
    "typedef", "const", "static", "extern", "inline", "restrict", "volatile", "register",
    "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "bv", "_BitVec",
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

TypeResolution good(Type::Kind k) {
    TypeResolution r;
    r.ok = true;
    r.type.kind = k;
    return r;
}

}  // namespace

std::string leanName(Type t) {
    switch (t.kind) {
    case Type::Kind::Literal:
    case Type::Kind::Int: return "Int";  // a literal with no context defaults to Int
    case Type::Kind::Nat: return "Nat";
    }
    return "";
}

std::string truthSuffix(Type t) {
    switch (t.kind) {
    case Type::Kind::Literal:
    case Type::Kind::Int:
    case Type::Kind::Nat: return " ≠ 0";
    }
    return "";
}

Type commonType(Type a, Type b) {
    if (a.kind == Type::Kind::Literal) return b;  // the literal takes the other side's type
    if (b.kind == Type::Kind::Literal) return a;
    if (a == b) return a;
    return Type{Type::Kind::Int};  // Int with Nat: Lean coerces Nat up to Int
}

TypeWord classifyTypeWord(const std::string& word) {
    if (kSupported.count(word)) return TypeWord::Supported;
    if (kRefused.count(word)) return TypeWord::Refused;
    return TypeWord::NotAType;
}

TypeResolution resolveTypeWords(const std::vector<std::string>& words) {
    const std::string spelled = joined(words);
    for (const std::string& w : words)
        if (classifyTypeWord(w) == TypeWord::Refused)
            return bad("type `" + spelled + "` is not translated in this slice",
                       "`" + w + "` exists in the main spec but is not translated yet; the type "
                       "table grows slice by slice.",
                       "use " + supportedTypesHint() + " for now.");

    // The integer family: C spells one type with several words in any order.
    int nUnsigned = 0, nSigned = 0, nShort = 0, nLong = 0, nInt = 0, nSizeT = 0;
    for (const std::string& w : words) {
        if (w == "unsigned") ++nUnsigned;
        else if (w == "signed") ++nSigned;
        else if (w == "short") ++nShort;
        else if (w == "long") ++nLong;
        else if (w == "int") ++nInt;
        else if (w == "size_t") ++nSizeT;
    }
    if (nUnsigned > 1 || nSigned > 1 || nShort > 1 || nInt > 1 || nLong > 2 || nSizeT > 1 ||
        (nUnsigned && nSigned) || (nShort && nLong) || (nSizeT && words.size() > 1))
        return bad("`" + spelled + "` is not a valid C type",
                   "C rejects this combination of type words.", "fix the spelling.");
    if (nUnsigned || nSizeT) return good(Type::Kind::Nat);
    return good(Type::Kind::Int);
}

std::string supportedTypesHint() { return "`int` or `unsigned` (or their `short`/`long` spellings)"; }
