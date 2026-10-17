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

struct Checker {
    const std::string& file;
    const std::map<std::string, size_t>& sigs;  // function name → arity
    Function& fn;
    // scopes[0] holds the parameters (nullptr) and the function's top-level
    // locals; a Decl is recorded by pointer so assignments can mark it `mut`.
    std::vector<std::map<std::string, Stmt*>> scopes;
    std::vector<bool> bare;  // per scope: opened by a bare `{ }` block (flattened in Lean)

    bool lookup(const std::string& name, Stmt*& decl) const {
        for (size_t k = scopes.size(); k-- > 0;) {
            auto it = scopes[k].find(name);
            if (it != scopes[k].end()) {
                decl = it->second;
                return true;
            }
        }
        return false;
    }

    // An expression whose value is an `Int`.
    void value(const Expr& e) {
        switch (e.kind) {
        case Expr::Kind::IntLit:
            return;
        case Expr::Kind::Ident: {
            Stmt* d;
            if (lookup(e.text, d)) return;
            if (sigs.count(e.text))
                fail(file, e.loc, "function `" + e.text + "` is used as a value",
                     "there are no function values (function pointers are ⛔ in the main spec).",
                     "call it: `" + e.text + "(…)`, or pass an argument.");
            fail(file, e.loc, "`" + e.text + "` is not a parameter, a visible local or a defined function",
                 "clean rejects unknown names instead of emitting Lean that fails later (D5).",
                 "declare it, define it, or fix the typo.");
        }
        case Expr::Kind::UnaryNeg:
            value(e.kids[0]);
            return;
        case Expr::Kind::Binary:
            value(e.kids[0]);
            value(e.kids[1]);
            return;
        case Expr::Kind::Call: {
            Stmt* d;
            if (lookup(e.text, d))
                fail(file, e.loc, "variable `" + e.text + "` is called like a function",
                     "function values are not translated.",
                     "call a defined function, or drop the call.");
            auto it = sigs.find(e.text);
            if (it == sigs.end())
                fail(file, e.loc, "`" + e.text + "` is called but not defined in this file",
                     "calls are resolved inside the file; `extern` declarations come later.",
                     "define `" + e.text + "` in the same file.");
            if (it->second != e.kids.size())
                fail(file, e.loc, "`" + e.text + "` takes " + std::to_string(it->second) +
                         " argument(s) but is called with " + std::to_string(e.kids.size()),
                     "the arity is checked here so that Lean does not have to report it.",
                     "match the parameter list.");
            for (const Expr& a : e.kids) value(a);
            return;
        }
        case Expr::Kind::Cmp:
        case Expr::Kind::And:
        case Expr::Kind::Or:
        case Expr::Kind::Not:
            fail(file, e.loc, "a comparison or logical operator is used as a number",
                 "in C it would be 0 or 1; clean keeps tests (Lean `Prop`) and numbers (`Int`) apart "
                 "instead of guessing a coercion.",
                 "use it as an `if` condition, e.g. `if (c) x = 1; else x = 0;`.");
        }
    }

    // An expression in a test position: a condition, or a number (≠ 0).
    void cond(const Expr& e) {
        switch (e.kind) {
        case Expr::Kind::Cmp:
            value(e.kids[0]);
            value(e.kids[1]);
            return;
        case Expr::Kind::And:
        case Expr::Kind::Or:
            cond(e.kids[0]);
            cond(e.kids[1]);
            return;
        case Expr::Kind::Not:
            cond(e.kids[0]);
            return;
        default:
            value(e);
        }
    }

    void markAssigned(const Stmt& s) {
        Stmt* d;
        if (!lookup(s.name, d)) {
            if (sigs.count(s.name))
                fail(file, s.loc, "assignment to the function `" + s.name + "`",
                     "functions are not assignable.", "assign to a local variable.");
            fail(file, s.loc, "assignment to `" + s.name + "`, which is not declared",
                 "clean rejects unknown names instead of emitting Lean that fails later (D5).",
                 "declare it first: `int " + s.name + " = …;`.");
        }
        if (d) {
            d->mut = true;
        } else {
            bool seen = false;
            for (const std::string& p : fn.mutParams) seen = seen || p == s.name;
            if (!seen) fn.mutParams.push_back(s.name);
        }
    }

    void scoped(std::vector<Stmt>& list, bool isBare = false) {
        scopes.emplace_back();
        bare.push_back(isBare);
        stmts(list);
        bare.pop_back();
        scopes.pop_back();
    }

    void switchStmt(Stmt& s) {
        value(s.expr);
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
                value(s.expr);  // the name is not in scope in its own initialiser
                if (scopes.back().count(s.name))
                    fail(file, s.loc, "`" + s.name + "` is declared twice in the same scope",
                         "C rejects it, and Lean would silently shadow it.", "rename one of them.");
                {
                    Stmt* outer;
                    if (bare.back() && lookup(s.name, outer))
                        fail(file, s.loc, "`" + s.name + "` in a bare `{ }` block hides a variable of the same name",
                             "Lean's nested `do` is not a scope, so the outer variable would be "
                             "wrongly replaced after the block.",
                             "rename the inner variable (`if`/`switch` bodies may shadow freely).");
                }
                scopes.back()[s.name] = &s;
                break;
            case Stmt::Kind::Assign:
                value(s.expr);
                markAssigned(s);
                break;
            case Stmt::Kind::If:
                cond(s.expr);
                scoped(s.body);
                scoped(s.elseBody);
                break;
            case Stmt::Kind::Return:
                value(s.expr);
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
            scopes[0][p.second] = nullptr;
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
    std::map<std::string, size_t> sigs;  // name → arity
    for (const Function& f : unit.functions) {
        if (sigs.count(f.name))
            fail(unit.fileName, Loc{f.defLine, 1}, "function `" + f.name + "` is defined twice",
                 "Lean allows only one definition per name.", "rename one of them.");
        sigs[f.name] = f.params.size();
    }
    for (Function& f : unit.functions) Checker{unit.fileName, sigs, f, {}, {}}.run();
}
