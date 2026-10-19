// check.cpp — implementation of check.h.

#include "check.h"

#include <cstdlib>
#include <iterator>
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

// ---- array-parameter writers (OD11, option c) ------------------------------
//
// C's `void fill(int a[], int n) { a[0] = 1; }` hands the caller back a
// modified array; Lean passes arrays by value, so the write would silently
// vanish. The decision (design-decisions.md, D20): such a function *returns*
// the new array — the emitted `def` has the written parameter's array type
// instead of `Unit`, and every call site re-assigns: `fill(x, n);` →
// `x := fill x n`.
//
// Which functions are writers cannot be decided one function at a time:
// passing your own array parameter to a writer writes it too, so the answer
// flows backwards along call edges. The scan below walks every body
// leniently — the scope rules of `Checker` (parameters in scope 0; every
// `if`/`else`/case/bare-block body its own scope; declarations shadow), but
// no diagnostics, because the bodies have not been checked yet. It collects:
//   * direct writes: an `Assign` with indices whose target is an array parameter;
//   * call edges: a call argument that names an array parameter of the caller.
// A fixpoint then propagates "the callee writes its parameter k" along those
// edges. What survives — exactly one written parameter and a `void` return —
// is a writer; anything else is refused (a tuple return is not built).

// One call site: the callee, which argument position, and the caller
// parameter passed there.
struct WriteEdge {
    size_t callee;  // index into the unit's functions
    size_t argPos;
    size_t param;   // the caller's parameter index
    Loc loc;
};

// The lenient walk over one body. Mirrors `Checker`'s scope discipline, minus
// every rule that reports; `Checker` runs later and judges for real.
struct WriterScan {
    const Function& fn;
    const std::map<std::string, size_t>& fnIndex;  // function name → its index
    std::map<size_t, Loc>& writes;                  // out: parameter index → first write seen
    std::vector<WriteEdge>& edges;                  // out
    std::vector<std::map<std::string, int>> scopes;  // name → parameter index, or -1 = a local

    // -2 = not visible, -1 = a local, >= 0 = the parameter's index
    int resolve(const std::string& name) const {
        for (size_t k = scopes.size(); k-- > 0;) {
            auto it = scopes[k].find(name);
            if (it != scopes[k].end()) return it->second;
        }
        return -2;
    }

    void noteWrite(size_t param, Loc loc) {
        if (fn.params[param].first.kind == Type::Kind::Array && !writes.count(param)) writes[param] = loc;
    }

    void scope(const std::vector<Stmt>& list) {
        scopes.emplace_back();
        scan(list);
        scopes.pop_back();
    }

    void scan(const std::vector<Stmt>& list) {
        for (const Stmt& s : list) {
            switch (s.kind) {
            case Stmt::Kind::Decl:
                scopes.back()[s.name] = -1;
                break;
            case Stmt::Kind::Assign: {
                const int p = resolve(s.name);
                if (!s.indices.empty() && p >= 0) noteWrite((size_t)p, s.loc);
                break;
            }
            case Stmt::Kind::CallStmt: {
                auto it = fnIndex.find(s.expr.text);
                if (it == fnIndex.end()) break;  // unknown callee: Checker refuses it later
                for (size_t k = 0; k < s.expr.kids.size(); ++k) {
                    const Expr& arg = s.expr.kids[k];
                    if (arg.kind != Expr::Kind::Ident) continue;
                    const int p = resolve(arg.text);
                    if (p >= 0 && fn.params[(size_t)p].first.kind == Type::Kind::Array)
                        edges.push_back(WriteEdge{it->second, k, (size_t)p, arg.loc});
                }
                break;
            }
            case Stmt::Kind::If:
                scope(s.body);
                scope(s.elseBody);
                break;
            case Stmt::Kind::Switch:
                for (const Case& c : s.cases) scope(c.body);
                break;
            case Stmt::Kind::Block:
                scope(s.body);
                break;
            default:
                break;  // Return, Break: nothing to gather
            }
        }
    }

    void run() {
        scopes.emplace_back();
        for (size_t k = 0; k < fn.params.size(); ++k) scopes[0][fn.params[k].second] = (int)k;
        scan(fn.body);
    }
};

// The pre-pass proper: scan, propagate, then set `writerParam` on the winners
// and refuse the shapes clean cannot express (one array out, `void` only).
void findWriters(TranslationUnit& unit, const std::string& file) {
    const size_t n = unit.functions.size();
    std::map<std::string, size_t> fnIndex;
    for (size_t k = 0; k < n; ++k) fnIndex[unit.functions[k].name] = k;

    std::vector<std::map<size_t, Loc>> writes(n);
    std::vector<std::vector<WriteEdge>> edges(n);
    for (size_t k = 0; k < n; ++k) WriterScan{unit.functions[k], fnIndex, writes[k], edges[k], {}}.run();

    bool changed = true;  // passing your parameter to a writer writes it — repeat until stable
    while (changed) {
        changed = false;
        for (size_t f = 0; f < n; ++f)
            for (const WriteEdge& e : edges[f])
                if (writes[e.callee].count(e.argPos) && !writes[f].count(e.param)) {
                    writes[f][e.param] = e.loc;
                    changed = true;
                }
    }

    for (size_t f = 0; f < n; ++f) {
        Function& fn = unit.functions[f];
        if (writes[f].empty()) continue;
        auto first = writes[f].begin();
        const std::string& pname = fn.params[first->first].second;
        if (fn.retType.kind != Type::Kind::Unit)
            fail(file, first->second,
                 "`" + fn.name + "` returns `" + cName(fn.retType) + "` and also writes to its array "
                 "parameter `" + pname + "`",
                 "in C the caller's array changes too, so clean would have to return both the "
                 "`" + cName(fn.retType) + "` value and the new array; a tuple return is not built "
                 "(main-spec §4.6 keeps it 🟡).",
                 "make the function `void` — the written array parameter then becomes its return "
                 "value — or pass a local array instead.");
        if (writes[f].size() > 1) {
            auto second = std::next(first);
            fail(file, second->second,
                 "`" + fn.name + "` writes to more than one array parameter (`" + pname + "` and `" +
                 fn.params[second->first].second + "`)",
                 "clean returns one modified array per function (D5: silently dropping the second "
                 "write would be a wrong translation, and a tuple return is not built).",
                 "write to one array parameter per function, or make the other a local array.");
        }
        fn.writerParam = (int)first->first;
    }
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
    const std::map<std::string, Sig>& sigs;  // function name → parameter types and return type
    Function& fn;
    const std::set<std::string>& pragmas;    // `#pragma lean …` words
    std::vector<DiagError>& notes;           // hints (H…) the unit carries to main
    const std::map<std::string, int>& writers;  // name → written array parameter's index (OD11 c)
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
        if (e.type.kind == Type::Kind::Unit)
            fail(file, e.loc, "the result of a `void` call is used as a value",
                 "a `void` function returns nothing.", "call it as a statement, on its own line.");
        return e.type;
    }

    // `e` has type `e.type`; make it `target` by inserting the conversion main-spec §5.3 names
    // (decision D19). A literal needs none: Lean reads the numeral at the type it is used at.
    void wrapCast(Expr& e, Type target) {
        Expr inner = std::move(e);
        e = Expr();
        e.kind = Expr::Kind::Cast;
        e.implicit = true;
        e.type = target;
        e.loc = inner.loc;
        e.kids.push_back(std::move(inner));
    }

    void coerce(Expr& e, Type target) {
        if (e.type == target || isLiteral(e.type)) return;
        std::string why, fix;
        if (convert(e.type, target, "x", &why, &fix).empty())
            fail(file, e.loc, "cannot convert `" + cName(e.type) + "` to `" + cName(target) + "`", why, fix);
        wrapCast(e, target);
    }

    Type valueType(Expr& e) {
        switch (e.kind) {
        case Expr::Kind::IntLit:
            return Type::of(Type::Kind::Literal);
        case Expr::Kind::FloatLit:
            return Type::of(Type::Kind::FloatLiteral);
        case Expr::Kind::CharLit:
            return Type::of(Type::Kind::Char);
        case Expr::Kind::StringLit:
            return Type::of(Type::Kind::String);
        case Expr::Kind::ArrayLit:
            fail(file, e.loc, "a `{ … }` list outside an array initialiser",
                 "main-spec §4.4 accepts it only as `T a[N] = { … };`.", "write the elements one by one.");
        case Expr::Kind::Index: {
            Type base = value(e.kids[0]);
            if (base.kind != Type::Kind::Array && base.kind != Type::Kind::String)
                fail(file, e.loc, "indexing a `" + cName(base) + "`", "only arrays and strings can be indexed.",
                     "index an array.");
            valueAs(e.kids[1], Type::of(Type::Kind::Nat));  // `a[i]!` takes a `Nat` (§5.3 conversion if needed)
            return base.kind == Type::Kind::String ? Type::of(Type::Kind::Char) : base.elem();
        }
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
            const bool shift = e.text == "<<" || e.text == ">>";
            const bool bitwise = shift || e.text == "&" || e.text == "|" || e.text == "^";
            if (bitwise && (l.kind == Type::Kind::Char || r.kind == Type::Kind::Char)) {
                OpRule op = checkBinaryOp(e.text, Type::of(Type::Kind::Char));  // H004
                failDiag(op.code, e.loc, op.problem, op.why, op.fix);
            }
            Type result;
            if (shift && !isLiteral(l)) {  // C: the result has the left operand's type; the count converts to it
                result = l;
                coerce(e.kids[1], l);
            } else {
                Unified u = unify(l, r);
                if (!u.ok) failDiag(u.code, e.loc, u.problem, u.why, u.fix);
                result = u.type;
                coerce(e.kids[0], result);
                coerce(e.kids[1], result);
            }
            OpRule op = checkBinaryOp(e.text, result);
            if (!op.ok) failDiag(op.code, e.loc, op.problem, op.why, op.fix);
            return result;
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

    // `a OP b` of a comparison: both sides converted to their common type (D19).
    void cmpOperands(Expr& e) {
        Type l = value(e.kids[0]);
        Type r = value(e.kids[1]);
        Unified u = unify(l, r, true);
        if (!u.ok) failDiag(u.code, e.loc, u.problem, u.why, u.fix);
        OpRule c = checkCompare(e.text, u.type);
        if (!c.ok) failDiag(c.code, e.loc, c.problem, c.why, c.fix);
        coerce(e.kids[0], u.type);
        coerce(e.kids[1], u.type);
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
        case Flow::Convert:
            wrapCast(e, target);
            return;
        case Flow::Refuse: {
            const bool bits = t.kind == Type::Kind::BitVec && target.kind == Type::Kind::BitVec;
            std::string why, fix;
            convert(t, target, "x", &why, &fix);
            if (isLiteral(t)) {
                why = "the literal would be truncated; clean does not guess.";
                fix = "write a value of the target type.";
            }
            failDiag(bits ? "E301" : "E000", e.loc,
                     "a value of type `" + cName(t) + "` is used where `" + cName(target) + "` is expected",
                     why, fix);
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

    void markAssigned(const std::string& name, Loc loc) {
        Var v;
        if (!lookup(name, v)) {
            if (sigs.count(name))
                fail(file, loc, "assignment to the function `" + name + "`",
                     "functions are not assignable.", "assign to a local variable.");
            fail(file, loc, "assignment to `" + name + "`, which is not declared",
                 "clean rejects unknown names instead of emitting Lean that fails later (D5).",
                 "declare it first: `int " + name + " = …;`.");
        }
        if (v.decl) {
            v.decl->mut = true;
        } else {
            bool seen = false;
            for (const std::string& p : fn.mutParams) seen = seen || p == name;
            if (!seen) fn.mutParams.push_back(name);
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

    // `{1, 2, 3}` / `{{1, 2}, {3, 4}}` as the initialiser of an array of type `arr`;
    // `dims[depth]` is the declared size at this level (-1 = take it from the list).
    void initList(Expr& lit, Type arr, std::vector<long long>& dims, size_t depth) {
        const long long k = (long long)lit.kids.size();
        if (dims[depth] >= 0 && k > dims[depth])
            fail(file, lit.loc, "too many initialisers: " + std::to_string(k) + " for an array of " +
                     std::to_string(dims[depth]),
                 "C rejects it too.", "drop the extra values, or enlarge the array.");
        if (dims[depth] < 0) {
            if (k == 0)
                fail(file, lit.loc, "an array with no size and no elements", "C rejects `a[] = {}`.",
                     "write the size or the elements.");
            dims[depth] = k;
        }
        lit.type = arr;
        const Type elem = arr.elem();
        for (Expr& kid : lit.kids) {
            if (elem.kind == Type::Kind::Array) {
                if (kid.kind != Expr::Kind::ArrayLit)
                    fail(file, kid.loc, "a row of the array must be a `{ … }` list", "the array has more than one dimension.",
                         "write `{ … }` for each row.");
                initList(kid, elem, dims, depth + 1);
            } else {
                if (kid.kind == Expr::Kind::ArrayLit)
                    fail(file, kid.loc, "a nested `{ … }` for a one-dimensional array",
                         "C would accept braces around a scalar; Lean has no such thing.", "drop the inner braces.");
                valueAs(kid, elem);
            }
        }
    }

    void declStmt(Stmt& s) {
        if (s.type.kind != Type::Kind::Array) {
            valueAs(s.expr, s.type);
            return;
        }
        for (size_t d = 1; d < s.dims.size(); ++d)
            if (s.dims[d] < 0)
                fail(file, s.loc, "only the first size of `" + s.name + "` may be left out",
                     "C rejects `a[2][]` too.", "write every size but the first.");
        if (!s.noInit) initList(s.expr, s.type, s.dims, 0);
        Type inner = s.type;
        while (inner.kind == Type::Kind::Array) inner = inner.elem();
        if (inner.kind == Type::Kind::Bool && s.dims.size() == 1 && s.dims[0] > 0 &&
            !pragmas.count("allow_bool_array")) {  // H001 (main-spec §4.4)
            const std::string n = std::to_string(s.dims[0]);
            notes.push_back(DiagError(
                "H001", file, s.loc, "`bool " + s.name + "[" + n + "]` — did you mean `bv(" + n + ") " + s.name + "`?",
                "a bit-vector gets Lean's BitVec tactics (bv_decide, omega on bit ops); `Array Bool` does not.",
                "rewrite as `bv(" + n + ") " + s.name + ";`, or silence with `#pragma lean allow_bool_array`."));
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
                declStmt(s);  // the name is not in scope in its own initialiser
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
                if (lookup(s.name, target)) {
                    Type t = target.type;
                    for (Expr& idx : s.indices) {  // `a[i][j] = v`: walk down to the element type
                        if (t.kind == Type::Kind::String)
                            failCode("E101", file, s.loc, "mutation of a string by index",
                                     "Lean strings are immutable (main-spec §4.3).",
                                     "use an `Array Char`, or `uint8_t`/`bv(N)` arrays for byte buffers.");
                        if (t.kind != Type::Kind::Array)
                            fail(file, s.loc, "indexing `" + s.name + "`, which is not an array",
                                 "only arrays can be indexed.", "index an array.");
                        valueAs(idx, Type::of(Type::Kind::Nat));
                        t = t.elem();
                    }
                    if (!s.indices.empty() && !target.decl &&
                        (fn.writerParam < 0 || fn.params[(size_t)fn.writerParam].second != s.name))
                        // OD11 (c): a write to an array parameter is legal exactly in a writer,
                        // through its one written parameter. findWriters refuses every other
                        // shape before this point, so this is belt and braces.
                        fail(file, s.loc, "`" + s.name + "` is an array parameter and is written to",
                             "C would change the caller's array. clean gives the modified array "
                             "back instead: a `void` function that writes exactly one array "
                             "parameter returns it (see design-decisions.md, D20) — this function "
                             "is not one.",
                             "make the function `void` and write to one array parameter, or copy "
                             "into a local array.");
                    if (s.indices.empty() && t.kind == Type::Kind::Array)
                        fail(file, s.loc, "assignment to the whole array `" + s.name + "`",
                             "C rejects it too.", "assign the elements one by one.");
                    valueAs(s.expr, t);
                }
                markAssigned(s.name, s.loc);  // refuses unknown names and functions
                break;
            }
            case Stmt::Kind::CallStmt: {
                s.expr.type = valueType(s.expr);  // name, arity and arguments
                if (s.expr.type.kind != Type::Kind::Unit)
                    fail(file, s.expr.loc, "the result of `" + s.expr.text + "(…)` is discarded",
                         "functions are pure in Lean; a discarded value does nothing "
                         "(effects arrive with `printf` and IO).",
                         "use the value: `T r = " + s.expr.text + "(…);`.");
                auto w = writers.find(s.expr.text);
                if (w != writers.end()) {  // the callee is a writer: the array comes back (OD11 c)
                    Expr& arg = s.expr.kids[(size_t)w->second];
                    if (arg.kind != Expr::Kind::Ident)
                        fail(file, arg.loc, "`" + s.expr.text + "(…)` writes the array it is passed, "
                             "but that argument is not a whole variable",
                             "the modified array comes back as the return value, so it must be "
                             "assigned back to whatever was passed — only a variable can receive it.",
                             "pass an array variable; for a row of a 2D array, copy it out first "
                             "and copy it back after the call.");
                    markAssigned(arg.text, arg.loc);  // `let mut` parameter / marks the local's Decl
                    s.writeBack = arg.text;
                }
                break;
            }
            case Stmt::Kind::If:
                cond(s.expr);
                scoped(s.body);
                scoped(s.elseBody);
                break;
            case Stmt::Kind::Return:
                if (fn.retType.kind == Type::Kind::Unit) {
                    if (!s.noValue)
                        fail(file, s.loc, "a `void` function returns a value", "`void` has no values.",
                             "write `return;`.");
                } else {
                    if (s.noValue)
                        fail(file, s.loc, "`return;` in `" + fn.name + "`, which returns `" + cName(fn.retType) + "`",
                             "Lean needs a value on every path.", "write `return value;`.");
                    valueAs(s.expr, fn.retType);
                }
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
        if (fn.retType.kind != Type::Kind::Unit && !returnsOnAllPaths(fn.body))
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
    findWriters(unit, unit.fileName);  // OD11 (c): sets writerParam, refuses what cannot be expressed
    std::map<std::string, int> writers;  // name → its written array parameter's index
    for (const Function& f : unit.functions)
        if (f.writerParam >= 0) writers[f.name] = f.writerParam;
    for (Function& f : unit.functions)
        Checker{unit.fileName, sigs, f, unit.pragmas, unit.notes, writers, {}, {}}.run();
}
