// lint.cpp — implementation of lint.h.

#include "lint.h"

#include <cstdlib>
#include <map>

namespace {

struct Hit {
    DiagError note;
    int more = 0;  // further places with the same code
};

bool isIntegral(Type t) {
    switch (t.kind) {
    case Type::Kind::Literal:
    case Type::Kind::Int:
    case Type::Kind::Nat:
    case Type::Kind::SInt:
    case Type::Kind::UInt:
    case Type::Kind::BitVec: return true;
    default: return false;
    }
}

bool mentionsInt(Type t) {
    if (t.kind == Type::Kind::Int) return true;
    return t.kind == Type::Kind::Array && mentionsInt(t.elem());
}

// A value that cannot be negative: a numeral, or something converted from an unsigned type.
bool nonNegative(const Expr& e) {
    if (e.kind == Expr::Kind::IntLit) return true;
    if (e.kind == Expr::Kind::Cast && !e.kids.empty()) {
        switch (e.kids[0].type.kind) {
        case Type::Kind::Nat:
        case Type::Kind::UInt:
        case Type::Kind::Char:
        case Type::Kind::Bool: return true;
        default: return false;
        }
    }
    return false;
}

// Value of an integer literal (`7`, `0x1F`, `0b11`).
unsigned long long literalValue(const std::string& t) {
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'b' || t[1] == 'B'))
        return std::strtoull(t.c_str() + 2, nullptr, 2);
    return std::strtoull(t.c_str(), nullptr, 0);
}

struct Linter {
    const std::string& file;
    std::map<std::string, Hit> hits;  // by code, first place wins

    void report(const char* code, Loc loc, const std::string& what, const std::string& why,
                const std::string& fix) {
        auto it = hits.find(code);
        if (it != hits.end()) {
            ++it->second.more;
            return;
        }
        hits.emplace(code, Hit{DiagError(code, file, loc, what, why, fix), 0});
    }

    void type(Type t, Loc loc, const std::string& where) {
        if (!mentionsInt(t)) return;
        report("W001", loc, "`" + where + "` is an `int`: Lean's `Int` is unbounded",
               "C's `int` wraps at 32 bits; clean maps the `int` family to `Int` and emulates nothing (D4).",
               "use `int32_t` (or another `intN_t`) where a fixed width is meant.");
    }

    void expr(const Expr& e) {
        for (const Expr& k : e.kids) expr(k);
        switch (e.kind) {
        case Expr::Kind::Binary: {
            const std::string& op = e.text;
            if (op == "-" && e.type.kind == Type::Kind::Nat)
                report("W002", e.loc, "`-` on `unsigned` stops at 0",
                       "Lean's `Nat` subtraction is 0 when the result would be negative; C wraps around.",
                       "guard it with a comparison, or use `int` / `uint32_t` "
                       "(the `--unsigned=int` option of main-spec §4.1 is not built yet).");
            const bool divides = op == "/" || op == "%";
            if (divides && isIntegral(e.type)) {
                const Expr& d = e.kids[1];
                if (d.kind == Expr::Kind::IntLit && literalValue(d.text) == 0)
                    report("W005", e.loc, "`" + op + "` by zero",
                           "in Lean `x / 0` is 0 and `x % 0` is `x`; in C it is undefined.",
                           "remove the division.");
                else if (d.kind != Expr::Kind::IntLit)
                    report("W005", e.loc, "`" + op + "` by a value that may be zero",
                           "in Lean `x / 0` is 0 and `x % 0` is `x`; in C it is undefined.",
                           "guard the divisor.");
            }
            if (divides && e.type.kind == Type::Kind::Int &&
                !(nonNegative(e.kids[0]) && nonNegative(e.kids[1])))
                report("W014", e.loc, "`" + op + "` on `int` rounds differently from C for negative operands",
                       "C truncates toward zero (`-7 / 2 = -3`, `-7 % 2 = -1`); Lean's `Int` division is "
                       "Euclidean (`-7 / 2 = -4`, `-7 % 2 = 1`) and clean keeps Lean's (D18).",
                       "make sure both operands are non-negative, or use `unsigned`; "
                       "C's truncating division is not emitted.");
            const bool arith = op == "+" || op == "-" || op == "*" || divides || op == "<<";
            if (arith && (e.type.kind == Type::Kind::SInt || e.type.kind == Type::Kind::UInt) &&
                e.type.width < 32)
                report("W008", e.loc, "`" + op + "` on `" + cName(e.type) + "` is done in " +
                           std::to_string(e.type.width) + " bits",
                       "C promotes it to `int` first; Lean computes in the exact width and wraps (§5.3).",
                       "cast to a wider type first if C's promotion is meant, e.g. `(uint32_t)a + b`.");
            break;
        }
        case Expr::Kind::Cast: {
            const Type from = e.kids[0].type, to = e.type;
            const bool fromNum = isIntegral(from);
            if ((from.kind == Type::Kind::Char && isIntegral(to)) || (to.kind == Type::Kind::Char && fromNum))
                report("W007", e.loc, "a `char` is converted " + std::string(to.kind == Type::Kind::Char ? "from" : "to") +
                           " an integer",
                       "Lean's `Char` is not a number; clean goes through `Nat` (`c.toNat`, `Char.ofNat n`), "
                       "as main-spec §4.2 describes.",
                       "use `uint8_t` for byte values.");
            if ((from.kind == Type::Kind::Int || from.kind == Type::Kind::SInt) &&
                (to.kind == Type::Kind::Nat || to.kind == Type::Kind::Char))
                report("W009", e.loc, "a signed value is converted to `" + cName(to) + "`; negatives become 0",
                       "Lean's `Int.toNat` clamps negative numbers to 0; C wraps modulo 2^N.",
                       "make sure the value is non-negative, or keep it signed.");
            break;
        }
        case Expr::Kind::Index:
            if (e.kids[0].type.kind == Type::Kind::String)
                report("W011", e.loc, "indexing a string is O(n)",
                       "`s.toList[i]!` builds the whole character list on every access.",
                       "avoid indexing in loops; use a `uint8_t` array for byte buffers.");
            else
                report("W010", e.loc, "`a[i]` panics when the index is out of range",
                       "it is `a[i]!`, which panics at run time.",
                       "guard the index with a comparison "
                       "(the `--index=proof` option of main-spec §10 is not built yet).");
            break;
        default:
            break;
        }
    }

    void stmts(const std::vector<Stmt>& list) {
        for (const Stmt& s : list) {
            if (s.kind == Stmt::Kind::Decl) type(s.type, s.loc, cName(s.type) + " " + s.name);
            if (s.kind == Stmt::Kind::Assign && !s.indices.empty())
                report("W010", s.loc, "`" + s.name + "[i] = v` ignores an out-of-range index",
                       "it is `a.set! i v`, which does nothing when `i` is out of range "
                       "(reading `a[i]!` panics instead).",
                       "guard the index with a comparison "
                       "(the `--index=proof` option of main-spec §10 is not built yet).");
            stmts(s.body);
            stmts(s.elseBody);
            for (const Case& c : s.cases) stmts(c.body);
        }
    }
};

}  // namespace

void lint(TranslationUnit& unit) {
    Linter L{unit.fileName, {}};
    for (const Function& f : unit.functions) {
        const Loc at{f.defLine, 1};
        L.type(f.retType, at, cName(f.retType) + " " + f.name + "(…)");
        for (const auto& p : f.params) L.type(p.first, at, cName(p.first) + " " + p.second);
        L.stmts(f.body);
        forEachExpr(f.body, [&](const Expr& e) { L.expr(e); });
    }
    for (auto& entry : L.hits) {
        Hit& h = entry.second;
        if (h.more == 0) {
            unit.notes.push_back(h.note);
            continue;
        }
        DiagError d = h.note;  // rebuild with the count in the headline
        unit.notes.push_back(DiagError(entry.first, unit.fileName, d.loc(),
                                       std::string(d.what()) + "  (+" + std::to_string(h.more) +
                                           " more in this file)",
                                       d.why(), d.fix()));
    }
}
