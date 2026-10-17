// check.cpp — implementation of check.h.

#include "check.h"

#include <cstdlib>
#include <map>
#include <set>

namespace {

[[noreturn]] void failCode(const char* code, const std::string& file, Loc loc,
                           const std::string& what, const std::string& why,
                           const std::string& fix) {
    throw DiagError(code, file, loc, what, why, fix);
}

[[noreturn]] void fail(const std::string& file, Loc loc, const std::string& what,
                       const std::string& why, const std::string& fix) {
    failCode("E000", file, loc, what, why, fix);
}

// ---- control flow ---------------------------------------------------------

bool returnsOnAllPaths(const std::vector<Stmt>& list);

bool stmtReturns(const Stmt& s) {
    switch (s.kind) {
    case Stmt::Kind::Return:
        return true;
    case Stmt::Kind::If:
        return !s.elseBody.empty() && returnsOnAllPaths(s.body) && returnsOnAllPaths(s.elseBody);
    case Stmt::Kind::Block:
        return returnsOnAllPaths(s.body);
    case Stmt::Kind::Switch: {
        bool hasDefault = false;
        for (const Case& c : s.cases) {
            if (c.isDefault) hasDefault = true;
            if (c.endsInBreak || !returnsOnAllPaths(c.body)) return false;
        }
        return hasDefault;
    }
    default:
        return false;
    }
}

bool returnsOnAllPaths(const std::vector<Stmt>& list) {
    for (const Stmt& s : list)
        if (stmtReturns(s)) return true;
    return false;
}

// Value of an integer-literal case label (`7`, `0x1F`, `0b11`, `-1`).
long long labelValue(const Expr& e) {
    if (e.kind == Expr::Kind::UnaryNeg) return -labelValue(e.kids[0]);
    const std::string& t = e.text;
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'b' || t[1] == 'B'))
        return (long long)std::strtoull(t.c_str() + 2, nullptr, 2);
    return (long long)std::strtoull(t.c_str(), nullptr, 0);  // decimal or 0x…
}

// ---- the checker ----------------------------------------------------------

struct Sig {
    std::vector<Type> params;
    Type ret;
};

// A visible name: its type, and its Decl (nullptr for a parameter) so
// assignments can mark the Decl `mut`.
struct Var {
    Stmt* decl;
    Type type;
};

struct Checker {
    const std::string& file;
    const std::map<std::string, Sig>& sigs;  // function name → arity and return type
    Function& fn;
    // scopes[0] holds the parameters and the function's top-level locals.
    std::vector<std::map<std::string, Var>> scopes;
    std::vector<bool> bare;  // per scope: opened by a bare `{ }` block (flattened in Lean)

    bool lookup(const std::string& name, Var& v) const {
        for (size_t k = scopes.size(); k-- > 0;) {
            auto it = scopes[k].find(name);
            if (it != scopes[k].end()) {
                v = it->second;
                return true;
            }
        }
        return false;
    }

    // ---- typing ---------------------------------------------------------------
    //
    // `value` decides the type of an expression and records it in `e.type`
    // (the one place expression types come from). `valueAs` is `value` plus
    // "this must end up as type T": it accepts, converts implicitly, or refuses
    // (types.h `flowKind`). Tests (`cond`) and bool-valued comparisons
    // (`boolValue`) are the only places where `< == && !` may appear.

    [[noreturn]] void failDiag(const std::string& code, Loc loc, const std::string& what,
                               const std::string& why, const std::string& fix) {
        failCode(code.c_str(), file, loc, what, why, fix);
    }

    static bool isTestOp(const Expr& e) {
        return e.kind == Expr::Kind::Cmp || e.kind == Expr::Kind::And ||
               e.kind == Expr::Kind::Or || e.kind == Expr::Kind::Not;
    }

    Type value(Expr& e) {
        e.type = valueType(e);
        return e.type;
    }

    Type valueType(Expr& e) {
        switch (e.kind) {
        case Expr::Kind::IntLit:
            return Type::of(Type::Kind::Literal);
        case Expr::Kind::FloatLit:
            return Type::of(Type::Kind::FloatLiteral);
        case Expr::Kind::CharLit:
            return Type::of(Type::Kind::Char);
        case Expr::Kind::BoolLit:
            return Type::of(Type::Kind::Bool);
        case Expr::Kind::BvLit:
            return e.type;  // the parser set the width
        case Expr::Kind::Ident: {
            Var v;
            if (lookup(e.text, v)) return v.type;
            if (sigs.count(e.text))
                fail(file, e.loc, "function `" + e.text + "` is used as a value",
                     "there are no function values (function pointers are ⛔ in the main spec).",
                     "call it: `" + e.text + "(…)`, or pass an argument.");
            fail(file, e.loc, "`" + e.text + "` is not a parameter, a visible local or a defined function",
                 "clean rejects unknown names instead of emitting Lean that fails later (D5).",
                 "declare it, define it, or fix the typo.");
        }
        case Expr::Kind::UnaryNeg:
        case Expr::Kind::BitNot: {
            Type t = value(e.kids[0]);
            OpRule r = checkUnaryOp(e.kind == Expr::Kind::UnaryNeg ? "-" : "~", t);
            if (!r.ok) failDiag(r.code, e.loc, r.problem, r.why, r.fix);
            return t;
        }
        case Expr::Kind::Cast: {
            Expr& kid = e.kids[0];
            Type from = isTestOp(kid) ? Type::of(Type::Kind::Bool) : value(kid);
            if (isTestOp(kid)) valueAs(kid, from);
            std::string why, fix;
            if (convert(from, e.type, "x", &why, &fix).empty())
                fail(file, e.loc, "cannot cast `" + cName(from) + "` to `" + cName(e.type) + "`", why, fix);
            return e.type;
        }
        case Expr::Kind::Binary: {
            Type l = value(e.kids[0]);
            Type r = value(e.kids[1]);
            Unified u = unify(l, r);
            if (!u.ok) failDiag(u.code, e.loc, u.problem, u.why, u.fix);
            OpRule op = checkBinaryOp(e.text, u.type);
            if (!op.ok) failDiag(op.code, e.loc, op.problem, op.why, op.fix);
            return u.type;
        }
        case Expr::Kind::Call: {
            Var v;
            if (lookup(e.text, v))
                fail(file, e.loc, "variable `" + e.text + "` is called like a function",
                     "function values are not translated.",
                     "call a defined function, or drop the call.");
            auto it = sigs.find(e.text);
            if (it == sigs.end())
                fail(file, e.loc, "`" + e.text + "` is called but not defined in this file",
                     "calls are resolved inside the file; `extern` declarations come later.",
                     "define `" + e.text + "` in the same file.");
            if (it->second.params.size() != e.kids.size())
                fail(file, e.loc, "`" + e.text + "` takes " + std::to_string(it->second.params.size()) +
                         " argument(s) but is called with " + std::to_string(e.kids.size()),
                     "the arity is checked here so that Lean does not have to report it.",
                     "match the parameter list.");
            for (size_t k = 0; k < e.kids.size(); ++k) valueAs(e.kids[k], it->second.params[k]);
            return it->second.ret;
        }
        case Expr::Kind::Cmp:
        case Expr::Kind::And:
        case Expr::Kind::Or:
        case Expr::Kind::Not:
            fail(file, e.loc, "a comparison or logical operator is used as a number",
                 "in C it would be 0 or 1; clean keeps tests and numbers apart instead of "
                 "guessing a coercion.",
                 "use it as an `if` condition (`if (c) x = 1; else x = 0;`), or store it in a `bool`.");
        }
        return e.type;  // unreachable: every case above returns or throws
    }

    // `a OP b` of a comparison: both sides numbers (or bools/chars) of one type.
    void cmpOperands(Expr& e) {
        Type l = value(e.kids[0]);
        Type r = value(e.kids[1]);
        Unified u = unify(l, r);
        if (!u.ok) failDiag(u.code, e.loc, u.problem, u.why, u.fix);
        const bool eq = e.text == "==" || e.text == "!=";
        if (u.type.kind == Type::Kind::Bool && !eq)
            fail(file, e.loc, "`" + e.text + "` on `bool`", "Lean's `Bool` has no order; only `==` and `!=`.",
                 "compare the underlying numbers instead.");
    }

    // A `bool`-valued expression: `a < b`, `p && q`, `!p`, or any `bool`.
    void boolValue(Expr& e) {
        switch (e.kind) {
        case Expr::Kind::Cmp:
            cmpOperands(e);
            break;
        case Expr::Kind::And:
        case Expr::Kind::Or:
            valueAs(e.kids[0], Type::of(Type::Kind::Bool));
            valueAs(e.kids[1], Type::of(Type::Kind::Bool));
            break;
        case Expr::Kind::Not:
            valueAs(e.kids[0], Type::of(Type::Kind::Bool));
            break;
        default:
            break;
        }
        e.type = Type::of(Type::Kind::Bool);
    }

    // `e` must end up with type `target`: as is, via an implicit conversion
    // (number → bool, literal → char), or refused.
    void valueAs(Expr& e, Type target) {
        if (isTestOp(e)) {
            if (target.kind != Type::Kind::Bool) value(e);  // throws the "used as a number" refusal
            boolValue(e);
            return;
        }
        Type t = value(e);
        switch (flowKind(t, target)) {
        case Flow::Same:
            if ((target.kind == Type::Kind::Nat || target.kind == Type::Kind::UInt) &&
                e.kind == Expr::Kind::UnaryNeg && e.kids[0].kind == Expr::Kind::IntLit)
                fail(file, e.loc, "a negative value for `" + cName(target) + "`",
                     "Lean's `" + leanName(target) + "` cannot hold it.",
                     "use a non-negative value, or an `int`/`intN_t` type.");
            return;
        case Flow::Convert: {
            Expr inner = std::move(e);
            e = Expr();
            e.kind = Expr::Kind::Cast;
            e.implicit = true;
            e.type = target;
            e.loc = inner.loc;
            e.kids.push_back(std::move(inner));
            return;
        }
        case Flow::Refuse: {
            const bool bits = t.kind == Type::Kind::BitVec && target.kind == Type::Kind::BitVec;
            failDiag(bits ? "E301" : "E000", e.loc,
                     "a value of type `" + cName(t) + "` is used where `" + cName(target) + "` is expected",
                     bits ? "a bit-vector operation needs equal widths; Lean rejects the rest."
                          : "Lean has no implicit conversion between them, and clean does not pick one (D5).",
                     "cast it explicitly: `(" + cName(target) + ")x`.");
        }
        }
    }

    // An expression in a test position: a condition, or a value (≠ 0).
    void cond(Expr& e) {
        switch (e.kind) {
        case Expr::Kind::Cmp:
            cmpOperands(e);
            return;
        case Expr::Kind::And:
        case Expr::Kind::Or:
            cond(e.kids[0]);
            cond(e.kids[1]);
            return;
        case Expr::Kind::Not:
            cond(e.kids[0]);
            return;
        default: {
            Type t = value(e);
            if (!hasTruth(t))
                fail(file, e.loc, "a `" + cName(t) + "` is used as a test",
                     "main-spec §5.2 has no truthiness rule for it.", "compare it explicitly.");
        }
        }
    }

    Type markAssigned(const Stmt& s) {
        Var v;
        if (!lookup(s.name, v)) {
            if (sigs.count(s.name))
                fail(file, s.loc, "assignment to the function `" + s.name + "`",
                     "functions are not assignable.", "assign to a local variable.");
            fail(file, s.loc, "assignment to `" + s.name + "`, which is not declared",
                 "clean rejects unknown names instead of emitting Lean that fails later (D5).",
                 "declare it first: `int " + s.name + " = …;`.");
        }
        if (v.decl) {
            v.decl->mut = true;
        } else {
            bool seen = false;
            for (const std::string& p : fn.mutParams) seen = seen || p == s.name;
            if (!seen) fn.mutParams.push_back(s.name);
        }
        return v.type;
    }

    void scoped(std::vector<Stmt>& list, bool isBare = false) {
        scopes.emplace_back();
        bare.push_back(isBare);
        stmts(list);
        bare.pop_back();
        scopes.pop_back();
    }

    void switchStmt(Stmt& s) {
        Type st = value(s.expr);
        if (st.kind != Type::Kind::Int && st.kind != Type::Kind::Nat && st.kind != Type::Kind::Literal)
            fail(file, s.expr.loc, "`switch` on a `" + cName(st) + "` is not translated yet",
                 "only `int`/`unsigned` scrutinees are; match patterns on the other types are unverified.",
                 "use an `if`/`else if` chain.");
        std::set<long long> seen;
        bool haveDefault = false;
        for (size_t k = 0; k < s.cases.size(); ++k) {
            Case& c = s.cases[k];
            if (c.isDefault) {
                if (haveDefault)
                    fail(file, c.loc, "more than one `default` in a switch",
                         "C rejects it too.", "merge the groups.");
                haveDefault = true;
            }
            for (const Expr& lab : c.labels)
                if (!seen.insert(labelValue(lab)).second)
                    fail(file, lab.loc, "duplicate `case` value",
                         "C rejects it too.", "remove or change one of the labels.");
            scoped(c.body);
            bool last = k + 1 == s.cases.size();
            if (!last && !c.endsInBreak && !returnsOnAllPaths(c.body))
                failCode("E105", file, c.loc, "switch case falls through into the next one",
                         "Lean's `match` arms never fall through, and emulating it would duplicate code.",
                         "end the case with `break;` or `return`, or merge the labels "
                         "(`case 1: case 2:`).");
        }
    }

    void stmts(std::vector<Stmt>& list) {
        bool dead = false;
        for (Stmt& s : list) {
            if (dead)
                fail(file, s.loc, "unreachable code after `return`",
                     "Lean rejects statements after a `return` in the same block.",
                     "delete it, or move the `return` below it.");
            switch (s.kind) {
            case Stmt::Kind::Decl:
                valueAs(s.expr, s.type);  // the name is not in scope in its own initialiser
                if (scopes.back().count(s.name))
                    fail(file, s.loc, "`" + s.name + "` is declared twice in the same scope",
                         "C rejects it, and Lean would silently shadow it.", "rename one of them.");
                {
                    Var outer;
                    if (bare.back() && lookup(s.name, outer))
                        fail(file, s.loc, "`" + s.name + "` in a bare `{ }` block hides a variable of the same name",
                             "Lean's nested `do` is not a scope, so the outer variable would be "
                             "wrongly replaced after the block.",
                             "rename the inner variable (`if`/`switch` bodies may shadow freely).");
                }
                scopes.back()[s.name] = Var{&s, s.type};
                break;
            case Stmt::Kind::Assign: {
                Var target;
                if (lookup(s.name, target)) valueAs(s.expr, target.type);
                markAssigned(s);  // refuses unknown names and functions
                break;
            }
            case Stmt::Kind::If:
                cond(s.expr);
                scoped(s.body);
                scoped(s.elseBody);
                break;
            case Stmt::Kind::Return:
                valueAs(s.expr, fn.retType);
                break;
            case Stmt::Kind::Switch:
                switchStmt(s);
                break;
            case Stmt::Kind::Block:
                scoped(s.body, true);
                break;
            case Stmt::Kind::Break:
                break;  // the parser already rejected the stray ones
            }
            if (stmtReturns(s)) dead = true;
        }
    }

    void run() {
        scopes.emplace_back();
        bare.push_back(false);
        std::set<std::string> seen;
        for (const auto& p : fn.params) {
            if (!seen.insert(p.second).second)
                fail(file, Loc{fn.defLine, 1}, "parameter `" + p.second + "` is declared twice",
                     "C rejects it too.", "rename one of them.");
            scopes[0][p.second] = Var{nullptr, p.first};
        }
        stmts(fn.body);
        if (!returnsOnAllPaths(fn.body))
            fail(file, Loc{fn.defLine, 1},
                 "`" + fn.name + "` can reach the end of its body without returning a value",
                 "C leaves that undefined, and Lean needs a value on every path.",
                 "end every path with `return …;` (a `switch` needs a `default`, an `if` an `else`).");
    }
};

}  // namespace

void check(TranslationUnit& unit) {
    std::map<std::string, Sig> sigs;
    for (const Function& f : unit.functions) {
        if (sigs.count(f.name))
            fail(unit.fileName, Loc{f.defLine, 1}, "function `" + f.name + "` is defined twice",
                 "Lean allows only one definition per name.", "rename one of them.");
        Sig sig;
        sig.ret = f.retType;
        for (const auto& p : f.params) sig.params.push_back(p.first);
        sigs[f.name] = sig;
    }
    for (Function& f : unit.functions) Checker{unit.fileName, sigs, f, {}, {}}.run();
}
