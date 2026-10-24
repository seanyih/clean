// check.cpp — implementation of check.h.
//
// Names, arities, scopes, types, and the loop decision. `for` (main-spec §6.1)
// is decided here because only the types say whether a header is a range-for:
// `for (T i = A; i < B; i++)` with a constant or unsigned/`Nat`-valued `A`/`B`
// and a body that leaves `i` alone becomes `for i in [A:B] do`; any other
// header is rewritten into the general form — initialiser before a `while`,
// the step at the end of the body and before every `continue` of that loop
// (a `Block` around both, so the emitted scope is one flat `do` sequence).
// The rewrite must not change which names things refer to, so two tree walks
// refuse the shapes where it would: a loop variable that hides an outer name
// (the flattened `let` would shadow it for the rest of the block), and a body
// local that shadows a name the injected step (or the re-tested `do … while`
// condition) was checked with.

#include "check.h"

#include <cstdlib>
#include <iterator>
#include <map>
#include <set>

#include "builtins.h"
#include "prelude.h"

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
    case Stmt::Kind::Exit:  // `exit(n)` never comes back: like a `return`, nothing may follow it
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

// ---- loops (main-spec §6.1) --------------------------------------------------

// The names an expression reads: its identifiers, not its callee names (a
// local that shadows a function is caught where the call is checked).
void freeIdents(const Expr& e, std::set<std::string>& out) {
    if (e.kind == Expr::Kind::Ident) out.insert(e.text);
    for (const Expr& k : e.kids) freeIdents(k, out);
}

// Does the list (or anything nested in it, other loops' parts included) write
// one of `names`? A write is a whole-variable assignment, an element write, or
// a call to a writer (D20) passing the name as its written argument — the
// call statement re-assigns what it passed. §6.1 forbids writing the loop
// variable of a range-for, and a range-for also needs the names its bounds
// read to stay untouched: the range freezes them at loop entry, while C
// re-tests the condition every iteration.
bool writesAny(const std::vector<Stmt>& list, const std::set<std::string>& names,
               const std::map<std::string, int>& writers) {
    for (const Stmt& s : list) {
        if (s.kind == Stmt::Kind::Assign && names.count(s.name)) return true;
        if (s.kind == Stmt::Kind::CallStmt) {
            auto w = writers.find(s.expr.text);
            if (w != writers.end() && w->second < (int)s.expr.kids.size()) {
                const Expr& arg = s.expr.kids[w->second];
                if (arg.kind == Expr::Kind::Ident && names.count(arg.text)) return true;
            }
        }
        if (writesAny(s.init, names, writers) || writesAny(s.step, names, writers)) return true;
        if (writesAny(s.body, names, writers) || writesAny(s.elseBody, names, writers)) return true;
        for (const Case& c : s.cases)
            if (writesAny(c.body, names, writers)) return true;
    }
    return false;
}

// One of the names `names` is redeclared by `shadowed` (both sets intersect).
bool shadowsAny(const std::set<std::string>& names, const std::set<std::string>& shadowed,
                std::string& which) {
    for (const std::string& n : names)
        if (shadowed.count(n)) {
            which = n;
            return true;
        }
    return false;
}

// The general `for` writes its step (a `do … while` its re-tested condition)
// where the body can have redeclared one of the names it was checked with, so
// the answer is a walk, not a glance: it tracks which of `names` each list
// redeclares (a declaration shadows to the end of its list, and into the
// lists nested after it, like a C scope), and reports the `continue`s and the
// body end — the points where the statement gets injected — that no longer
// see the original binding. `continue`s and ends of *other* loops do not
// count: they get nothing injected, their own step/condition did.
struct InjectScan {
    std::set<std::string> names;
    bool bad = false;
    Loc loc;
    std::string which;

    void walk(const std::vector<Stmt>& list, std::set<std::string> shadowed, bool injectable,
              bool isTop, Loc end) {
        for (const Stmt& s : list) {
            if (s.kind == Stmt::Kind::Decl && names.count(s.name)) shadowed.insert(s.name);
            if (s.kind == Stmt::Kind::Continue && injectable &&
                shadowsAny(names, shadowed, which)) {
                bad = true;
                loc = s.loc;
                return;
            }
            switch (s.kind) {
            case Stmt::Kind::If:
                walk(s.body, shadowed, injectable, false, end);
                walk(s.elseBody, shadowed, injectable, false, end);
                break;
            case Stmt::Kind::Block:
                walk(s.body, shadowed, injectable, false, end);
                break;
            case Stmt::Kind::Switch:
                for (const Case& c : s.cases) walk(c.body, shadowed, injectable, false, end);
                break;
            case Stmt::Kind::While:
            case Stmt::Kind::DoWhile:
                walk(s.body, shadowed, false, false, end);
                break;
            case Stmt::Kind::For: {
                std::set<std::string> inner = shadowed;
                if (!s.init.empty() && s.init[0].kind == Stmt::Kind::Decl &&
                    names.count(s.init[0].name))
                    inner.insert(s.init[0].name);  // shadows for this loop's body
                walk(s.body, inner, false, false, end);
                break;
            }
            default:
                break;
            }
        }
        if (isTop && shadowsAny(names, shadowed, which)) {  // the statement lands at the body's end
            bad = true;
            loc = end;
        }
    }
};

// A copy of the step before every `continue` that binds this loop — everywhere
// in the body except inside another loop (whose `continue`s are its own). The
// caller appends the step at the very end of the top-level body itself.
void injectStep(std::vector<Stmt>& list, const Stmt& step) {
    for (size_t k = 0; k < list.size(); ++k) {
        Stmt& s = list[k];
        switch (s.kind) {
        case Stmt::Kind::Continue:
            list.insert(list.begin() + (long)k, step);
            ++k;  // step over the copy
            break;
        case Stmt::Kind::If:
            injectStep(s.body, step);
            injectStep(s.elseBody, step);
            break;
        case Stmt::Kind::Block:
            injectStep(s.body, step);
            break;
        case Stmt::Kind::Switch:
            for (Case& c : s.cases) injectStep(c.body, step);
            break;
        default:
            break;  // While/DoWhile/For: their `continue`s bind them, not this loop
        }
    }
}

// The last statement of the flattened stream (bare blocks splice open), for
// "the body already ends in `continue`" — the step appended after it could
// never run.
bool endsInContinue(const std::vector<Stmt>& list) {
    if (list.empty()) return false;
    const Stmt* s = &list.back();
    while (s->kind == Stmt::Kind::Block) {
        if (s->body.empty()) return false;
        s = &s->body.back();
    }
    return s->kind == Stmt::Kind::Continue;
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
            case Stmt::Kind::For:
                // one scope for the whole loop: the variable the initialiser
                // declares is visible in the step and the body (check.cpp's
                // For does the same).
                scopes.emplace_back();
                scan(s.init);
                scan(s.step);
                scan(s.body);
                scopes.pop_back();
                break;
            case Stmt::Kind::While:
            case Stmt::Kind::DoWhile:
                scope(s.body);
                break;
            default:
                break;  // Return, Break, Continue: nothing to gather
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

// ---- I/O (main-spec §3.3, §8) ----------------------------------------------------
//
// A function is `IO` when it prints, or calls a function that does; `main` is
// always `IO`. Like the writers above, the answer flows backwards along call
// edges, so it is a fixpoint over the whole file, run before the per-function
// checks (which need to know which calls need `(← f x)`).

// A statement that is I/O by itself: printing, reading, closing a stream, or leaving the program.
bool hasPrint(const std::vector<Stmt>& list) {
    for (const Stmt& s : list) {
        if ((s.kind == Stmt::Kind::Print && s.targets.empty()) ||  // sprintf / snprintf only build a string
            s.kind == Stmt::Kind::Exit || s.kind == Stmt::Kind::Scan ||
            s.kind == Stmt::Kind::ReadLine || s.kind == Stmt::Kind::Close)
            return true;
        if (hasPrint(s.body) || hasPrint(s.elseBody)) return true;
        for (const Case& c : s.cases)
            if (hasPrint(c.body)) return true;
    }
    return false;
}

bool callsAny(const Expr& e, const std::set<std::string>& names) {
    if (e.kind == Expr::Kind::Call && names.count(e.text)) return true;
    for (const Expr& k : e.kids)
        if (callsAny(k, names)) return true;
    return false;
}

void findIO(TranslationUnit& unit) {
    std::set<std::string> io = {"getchar", "fgetc", "getc", "fopen"};  // library calls that touch a stream
    for (Function& f : unit.functions) {
        f.isIO = f.name == "main" || hasPrint(f.body);
        if (f.isIO) io.insert(f.name);
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (Function& f : unit.functions) {
            if (f.isIO) continue;
            bool calls = false;
            forEachExpr(f.body, [&](const Expr& e) { calls = calls || callsAny(e, io); });
            if (calls) {
                f.isIO = true;
                io.insert(f.name);
                changed = true;
            }
        }
    }
}

// `main` (main-spec §7.1): `int main(void)` is `IO UInt32`, `void main(void)` is `IO Unit`.
// The int version may fall off the end (C99: that returns 0), so the 0 is written in here.
void prepareMain(TranslationUnit& unit) {
    for (Function& f : unit.functions) {
        if (f.name != "main") continue;
        const Loc at{f.defLine, 1};
        if (!f.params.empty()) {  // exactly `(int argc, char *argv[])` (the parser typed argv as Array String)
            const bool ok = f.params.size() == 2 && f.params[0].first.kind == Type::Kind::Int &&
                            f.params[1].first == Type::arrayOf(Type::of(Type::Kind::String));
            if (!ok)
                fail(unit.fileName, at, "`main` takes parameters other than `(int argc, char *argv[])`",
                     "Lean's `main` gets the arguments as one `List String`; clean builds `argc` and `argv` from it, "
                     "and only in that pair.",
                     "write `int main(void)` or `int main(int argc, char *argv[])`.");
            f.mainArgs = true;
        }
        if (f.retType.kind == Type::Kind::Int) {
            f.retType = Type::of(Type::Kind::UInt, 32);  // `IO UInt32`
            if (!returnsOnAllPaths(f.body)) {
                Stmt ret;
                ret.kind = Stmt::Kind::Return;
                ret.loc = at;
                ret.expr.kind = Expr::Kind::IntLit;
                ret.expr.text = "0";
                ret.expr.loc = at;
                f.body.push_back(std::move(ret));
            }
        } else if (f.retType.kind != Type::Kind::Unit) {
            fail(unit.fileName, at, "`main` returns `" + cName(f.retType) + "`",
                 "main-spec §7.1 knows `int main` (`IO UInt32`) and `void main` (`IO Unit`).",
                 "return `int`, or `void`.");
        }
    }
}

// ---- the checker ----------------------------------------------------------

struct Sig {
    std::vector<Type> params;
    Type ret;
    bool io = false;  // the function does I/O (findIO)
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
    std::set<std::string>& helpers;             // the generated helpers this file needs (prelude.h)
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
        if (e.type == target) return;
        if (isLiteral(e.type)) {
            // a negative literal adapting to an unsigned type: the same
            // refusal `valueAs` gives `x = -1`, for the ones nested in
            // arithmetic (`x += -2`, `n % (-2)`, a shift count, a comparison…)
            if (e.kind == Expr::Kind::UnaryNeg && e.kids[0].kind == Expr::Kind::IntLit &&
                (target.kind == Type::Kind::Nat || target.kind == Type::Kind::UInt))
                fail(file, e.loc, "a negative value for `" + cName(target) + "`",
                     "Lean's `" + leanName(target) + "` cannot hold it.",
                     "use a non-negative value, or an `int`/`intN_t` type.");
            return;
        }
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
            if (isStandardStream(e.text)) {  // `(← IO.getStdin)` and friends: reading the stream is an action
                e.io = true;
                return Type::of(Type::Kind::File);
            }
            if (e.text == "NULL")
                fail(file, e.loc, "`NULL` is used",
                     "clean has no null pointers: `fopen` that fails stops the program with an error, "
                     "and strings and arrays are never null.",
                     "remove the check.");
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
            LibSignature lib;
            if (libSignature(e.text, lib)) {
                if (lib.params.size() != e.kids.size())
                    fail(file, e.loc, "`" + e.text + "` takes " + std::to_string(lib.params.size()) +
                             " argument(s) but is called with " + std::to_string(e.kids.size()),
                         "the arity is checked here so that Lean does not have to report it.",
                         "match the C library's signature.");
                for (size_t k = 0; k < e.kids.size(); ++k) valueAs(e.kids[k], lib.params[k]);
                if (e.text == "fgetc" || e.text == "getc") streamDirection(e.kids[0], false, e.text);
                if (e.text == "getc") e.text = "fgetc";  // the same function
                if (e.text == "fopen") {  // the mode has to be known now: it picks the Lean `Mode`
                    const Expr& m = e.kids[1];
                    const std::string mode = m.kind == Expr::Kind::StringLit ? m.text.substr(1, m.text.size() - 2) : "";
                    if (mode != "r" && mode != "w" && mode != "a" && mode != "rb" && mode != "wb" && mode != "ab")
                        fail(file, m.loc, "`fopen` with a mode clean does not translate",
                             "only the plain modes \"r\", \"w\" and \"a\" (also with `b`) exist; "
                             "update modes (`+`) need a read-write handle, which is not built.",
                             "use \"r\", \"w\" or \"a\" as a string literal.");
                }
                e.io = true;
                helpers.insert(e.text);  // getchar, fgetc, fopen: each is a generated helper (prelude.h)
                return lib.ret;
            }
            if (isLibraryName(e.text))
                fail(file, e.loc, "the result of `" + e.text + "(…)` is used as a value",
                     "`" + e.text + "` is translated as an `IO` action, which has no number to give back.",
                     "call it as a statement, on its own line.");
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
            e.io = it->second.io;
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

    // `a && f(x)` short-circuits in C; Lean's `(← f x)` is run before the `&&`, always.
    void noIO(const Expr& e) {
        if (e.kind == Expr::Kind::Call && e.io)
            fail(file, e.loc, "the I/O function `" + e.text + "` is called inside `&&` / `||`",
                 "C skips the right side when the left decides; Lean would run the call every time, "
                 "so the output would differ.",
                 "call it first and keep the result: `int r = " + e.text + "(…);`.");
        for (const Expr& k : e.kids) noIO(k);
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
            noIO(e.kids[1]);
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
            noIO(e.kids[1]);
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

    // `name = e;` / `name[i] = e;` — the Assign case of `stmts`, also the
    // shape of a `for` header part.
    void assignStmt(Stmt& s) {
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
    }

    // `f(args);` — the CallStmt case of `stmts`, also a `for` header part.
    void callStmt(Stmt& s) {
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
    }

    // ---- loops (main-spec §6) ---------------------------------------------------

    // True when the expression contains a call to an `IO` function (its
    // `io` flag is set by the typing pass).
    static bool callsIO(const Expr& e) {
        if (e.kind == Expr::Kind::Call && e.io) return true;
        for (const Expr& k : e.kids)
            if (callsIO(k)) return true;
        return false;
    }

    // A bound a range can use (§6.1): a non-negative integer literal, or an
    // expression whose own type is `unsigned`/`uintN_t`. A bound of another
    // type — an `int`-valued one included, §14.2 — falls to the general form,
    // as does a bound that does I/O (a range evaluates its bounds once, and
    // C could not write them anyway). The expression must already be typed.
    static bool rangeBound(const Expr& e) {
        if (e.kind == Expr::Kind::IntLit) return true;
        if (e.kind == Expr::Kind::Cast) return false;  // a written cast: its value is up to C
        return e.type.kind == Type::Kind::Nat || e.type.kind == Type::Kind::UInt;
    }

    // `for (…)` (§6.1): decide range-for vs the general form.
    void forStmt(Stmt& s) {
        // ---- the pattern, piece by piece; any miss falls to the general form ----
        bool ok = s.init.size() == 1 && s.init[0].kind == Stmt::Kind::Decl &&
                  s.init[0].dims.empty() &&
                  (s.init[0].type.kind == Type::Kind::Int ||
                   s.init[0].type.kind == Type::Kind::Nat ||
                   s.init[0].type.kind == Type::Kind::UInt);
        if (ok) {  // `i < B` or `i <= B`, the loop variable on the left
            ok = s.expr.kind == Expr::Kind::Cmp && (s.expr.text == "<" || s.expr.text == "<=") &&
                 s.expr.kids[0].kind == Expr::Kind::Ident &&
                 s.expr.kids[0].text == s.init[0].name;
        }
        if (ok) ok = !callsIO(s.init[0].expr) && !callsIO(s.expr.kids[1]);
        if (ok) {
            value(s.init[0].expr);  // the low bound's own type
            ok = rangeBound(s.init[0].expr);
        }
        if (ok) {
            value(s.expr.kids[1]);  // the high bound's own type
            ok = rangeBound(s.expr.kids[1]);
        }
        if (ok) {  // no step, or `i` on the left of `+` with a literal K ≥ 1
            ok = s.step.empty() ||
                 (s.step[0].kind == Stmt::Kind::Assign && s.step[0].indices.empty() &&
                  s.step[0].name == s.init[0].name &&
                  s.step[0].expr.kind == Expr::Kind::Binary && s.step[0].expr.text == "+" &&
                  s.step[0].expr.kids[0].kind == Expr::Kind::Ident &&
                  s.step[0].expr.kids[0].text == s.init[0].name &&
                  s.step[0].expr.kids[1].kind == Expr::Kind::IntLit &&
                  labelValue(s.step[0].expr.kids[1]) >= 1);
        }
        if (ok) {  // §6.1: the body leaves the loop variable alone — and, because
            // the range freezes its bounds at entry while C re-tests them every
            // iteration, it writes nothing the bounds read either (§6.1.2's
            // `while` is the exact form for those loops)
            std::set<std::string> header{s.init[0].name};
            freeIdents(s.init[0].expr, header);
            freeIdents(s.expr.kids[1], header);
            ok = !writesAny(s.body, header, writers);
        }

        if (ok) {  // ---- range-for: `for i in [A:B] do`, the header consumed ----
            s.range = true;
            s.inclusive = s.expr.text == "<=";
            declStmt(s.init[0]);  // the declaration's own conversions (§5.3); the emitter
                                  // peels them off the low bound — the range reads the Nat
            // The range yields `Nat`. An `int` loop variable keeps its C type:
            // the emitter rebinds it (`let i : Int := ↑i`, §6.1); `unsigned` and
            // `uintN_t` see the `Nat` itself, converting at use sites (§14.2).
            scopes.emplace_back();
            bare.push_back(false);
            scopes.back()[s.init[0].name] = Var{
                nullptr, s.init[0].type.kind == Type::Kind::Int ? Type::of(Type::Kind::Int)
                                                               : Type::of(Type::Kind::Nat)};
            scoped(s.body);
            bare.pop_back();
            scopes.pop_back();
            return;
        }

        // ---- general form: `{ init; while (cond) { body; step } }` ----
        if (!s.init.empty() && s.init[0].kind == Stmt::Kind::Decl) {
            Var outer;
            if (lookup(s.init[0].name, outer))
                fail(file, s.init[0].loc,
                     "the loop variable `" + s.init[0].name + "` hides an outer variable of the same name",
                     "the general `for` is flattened into its parent block, so the outer `" +
                         s.init[0].name + "` would be wrongly replaced after the loop.",
                     "rename the loop variable.");
        }
        if (!s.step.empty()) {  // the injected step must still see the names it was checked with
            InjectScan scan;
            const Stmt& st = s.step[0];
            scan.names.insert(st.name);
            for (const Expr& ix : st.indices) freeIdents(ix, scan.names);
            freeIdents(st.expr, scan.names);
            scan.walk(s.body, {}, true, true, s.loc);
            if (scan.bad)
                fail(file, scan.loc,
                     "`" + scan.which + "` is redeclared where the for step must run",
                     "the general `for` repeats its step at the end of the body and before "
                     "every `continue`, where the redeclared `" + scan.which +
                         "` would shadow the one the step was checked with.",
                     "rename the local, or drop the `continue` between them.");
        }
        Stmt blk;
        blk.kind = Stmt::Kind::Block;
        blk.loc = s.loc;
        blk.comments = std::move(s.comments);
        Stmt w;
        w.kind = Stmt::Kind::While;
        w.loc = s.loc;
        w.expr = std::move(s.expr);
        w.body = std::move(s.body);
        w.bodyTrailing = std::move(s.bodyTrailing);
        if (!s.step.empty()) {
            injectStep(w.body, s.step[0]);
            if (!endsInContinue(w.body)) w.body.push_back(s.step[0]);  // a trailing `continue` already ran it
        }
        if (!s.init.empty()) blk.body.push_back(std::move(s.init[0]));
        blk.body.push_back(std::move(w));
        s = std::move(blk);
        scoped(s.body);  // one scope for the loop variable; the `while` case takes it from there
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
            // a case that ends in `break` (stripped by the parser), `return` or —
            // inside a loop — `continue` does not fall through into the next one
            if (!last && !c.endsInBreak && !returnsOnAllPaths(c.body) && !endsInContinue(c.body))
                failCode("E105", file, c.loc, "switch case falls through into the next one",
                         "Lean's `match` arms never fall through, and emulating it would duplicate code.",
                         "end the case with `break;`, `continue;` or `return`, or merge the labels "
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

    // `printf` / `fprintf` / `puts` / `putchar` (main-spec §8.1): every argument must be the
    // kind its conversion reads. Mixed types are converted as everywhere (D19); what has no
    // sensible reading (a `bool` for `%d`, an `int` for `%f`) is refused, not guessed.
    void printStmt(Stmt& s) {
        if (!s.targets.empty()) {  // sprintf / snprintf: `buf := s!"…"`, a plain string assignment
            Var v;
            if (!lookup(s.targets[0], v))
                fail(file, s.targetLocs[0], "`" + s.targets[0] + "` is not declared",
                     "clean rejects unknown names instead of emitting Lean that fails later (D5).",
                     "declare it first: `char " + s.targets[0] + "[64] = \"\";`.");
            if (v.type.kind != Type::Kind::String)
                fail(file, s.targetLocs[0], "`" + s.name + "` fills `" + s.targets[0] + "`, which is a `" + cName(v.type) + "`",
                     "the text is a Lean `String`; arrays of characters are not translated.",
                     "declare it as a string: `char " + s.targets[0] + "[64] = \"\";`.");
            if (s.hasLimit) valueAs(s.limit, Type::of(Type::Kind::Nat));
            markAssigned(s.targets[0], s.targetLocs[0]);
        }
        if (s.hasStream) {
            valueAs(s.stream, Type::of(Type::Kind::File));
            streamDirection(s.stream, true, s.name);
        }
        if (s.name == "puts" || s.name == "fputs") {
            valueAs(s.args[0], Type::of(Type::Kind::String));
            return;
        }
        if (s.name == "putchar" || s.name == "fputc" || s.name == "putc") {
            valueAs(s.args[0], Type::of(Type::Kind::Char));
            return;
        }
        size_t next = 0;
        for (const FmtPiece& p : s.pieces) {
            if (!p.conv) continue;
            Expr& a = s.args[next++];
            const Type nat = Type::of(Type::Kind::Nat);
            auto refuse = [&](const Type& t, const std::string& why, const std::string& fix) {
                fail(file, a.loc, "a value of type `" + cName(t) + "` is printed with `%" + std::string(1, p.conv) + "`",
                     why, fix);
            };
            switch (p.conv) {
            case 's':
                valueAs(a, Type::of(Type::Kind::String));
                break;
            case 'c':
                valueAs(a, Type::of(Type::Kind::Char));
                break;
            case 'd':
            case 'u': {
                Type t = value(a);
                switch (t.kind) {
                case Type::Kind::Literal: case Type::Kind::Int: case Type::Kind::Nat:
                case Type::Kind::SInt: case Type::Kind::UInt: case Type::Kind::BitVec:
                    break;
                case Type::Kind::Char:
                    coerce(a, nat);  // C prints the character code
                    break;
                default:
                    refuse(t, "`%d` and `%u` print integers; clean does not guess how to show anything else.",
                           "use `%s`, `%c` or `%f`, or convert the value to an integer first.");
                }
                break;
            }
            case 'f': {
                Type t = value(a);
                if (!isFloat(t) && t.kind != Type::Kind::FloatLiteral)
                    refuse(t, "`%f` `%g` `%e` print floating-point values; C leaves an integer there undefined.",
                           "cast it: `(double)x`.");
                break;
            }
            case 'x':
            case 'X': {
                Type t = value(a);
                switch (t.kind) {
                case Type::Kind::BitVec:  // `x.toHex`: no helper needed
                    break;
                case Type::Kind::Literal: case Type::Kind::Nat:
                    helpers.insert("hexOf");
                    break;
                case Type::Kind::Int: case Type::Kind::SInt: case Type::Kind::UInt:
                    coerce(a, nat);  // the digits of the number (a negative `int` clamps to 0, lint W009)
                    helpers.insert("hexOf");
                    break;
                default:
                    refuse(t, "`%x` prints an integer in hexadecimal.",
                           "convert the value to an integer first.");
                }
                break;
            }
            }
        }
    }

    // A stream used for writing must not be `stdin`; one used for reading must not be `stdout` /
    // `stderr`. Only the three standard streams are known by name; a `FILE *` variable is trusted
    // (Lean reports a wrong-direction handle when the program runs).
    void streamDirection(const Expr& stream, bool writing, const std::string& fn) {
        if (stream.kind != Expr::Kind::Ident || !stream.io) return;
        if (writing && stream.text == "stdin")
            fail(file, stream.loc, "`" + fn + "` writes to `stdin`", "standard input can only be read.",
                 "write to `stdout` or `stderr`, or to a file you opened with \"w\".");
        if (!writing && stream.text != "stdin")
            fail(file, stream.loc, "`" + fn + "` reads from `" + stream.text + "`",
                 "standard output and error can only be written.",
                 "read from `stdin`, or from a file you opened with \"r\".");
    }

    // `scanf` / `fscanf` (main-spec §8.1, §12.6): each conversion reads one word of the input into
    // the variable it names. The variable keeps its value when nothing could be read (end of input,
    // or a word that is not a number), which is what C does too. The type decides how the word is read.
    void scanStmt(Stmt& s) {
        if (s.hasStream) {
            valueAs(s.stream, Type::of(Type::Kind::File));
            streamDirection(s.stream, false, s.name);
        }
        size_t next = 0;
        bool spaceBefore = false;  // a whitespace literal came right before the conversion
        for (const FmtPiece& p : s.pieces) {
            if (!p.conv) {
                spaceBefore = true;
                continue;
            }
            const size_t k = next++;
            s.targetSkipWs.push_back(spaceBefore);
            spaceBefore = false;
            const std::string& name = s.targets[k];
            const Loc at = s.targetLocs[k];
            Var v;
            if (!lookup(name, v))
                fail(file, at, "`" + name + "` is not declared",
                     "clean rejects unknown names instead of emitting Lean that fails later (D5).",
                     "declare it first: `int " + name + " = 0;`.");
            const std::string conv = "%" + std::string(1, p.conv == 'f' ? 'f' : p.conv);
            auto bad = [&](const std::string& why, const std::string& fix) {
                fail(file, at, "`" + name + "` (`" + cName(v.type) + "`) is read with `" + conv + "`", why, fix);
            };
            const bool strConv = p.conv == 's';
            if (strConv == s.targetAddr[k])
                fail(file, at, strConv ? "`&` before the string `" + name + "`" : "`" + name + "` is read without `&`",
                     strConv ? "a `char` string is not stored through its address in clean; `%s` takes the name itself."
                             : "the conversion stores into the variable, so C needs its address.",
                     strConv ? "write `" + name + "`." : "write `&" + name + "`.");
            switch (p.conv) {
            case 'd':
                if (v.type.kind != Type::Kind::Int && v.type.kind != Type::Kind::Nat)
                    bad("`%d` reads an integer into an `int` (or `unsigned`); other types need a conversion "
                        "clean does not guess.",
                        "read into an `int`, then convert: `x = (T)n;`.");
                break;
            case 'u':
                if (v.type.kind != Type::Kind::Nat && v.type.kind != Type::Kind::Int)
                    bad("`%u` reads a non-negative integer into an `unsigned` (or `int`).",
                        "read into an `unsigned`, then convert.");
                break;
            case 'f':
                if (!isFloat(v.type))
                    bad("`%f` `%g` `%e` read a floating-point number into a `float` or `double`.",
                        "declare the variable as `double`.");
                break;
            case 's':
                if (v.type.kind != Type::Kind::String)
                    bad("`%s` reads a word into a string (`char *`).", "declare the variable as `char s[] = \"\";`.");
                break;
            case 'c':
                if (v.type.kind != Type::Kind::Char)
                    bad("`%c` reads one character into a `char`.", "declare the variable as `char`.");
                break;
            }
            s.targetTypes.push_back(v.type);
            helpers.insert(p.conv == 'd' || p.conv == 'u' ? (v.type.kind == Type::Kind::Nat ? "scanNat" : "scanInt")
                           : p.conv == 'f' ? "scanFloat" : p.conv == 's' ? "scanStr"
                           : s.targetSkipWs[k] ? "scanCharWs" : "scanChar");
            markAssigned(name, at);  // `let mut` / a `let mut` copy of a parameter
        }
    }

    // `fgets(s, n, f);` reads a line into the string variable `s`.
    void readLineStmt(Stmt& s) {
        valueAs(s.stream, Type::of(Type::Kind::File));
        streamDirection(s.stream, false, "fgets");
        valueAs(s.args[0], Type::of(Type::Kind::Nat));
        Var v;
        if (!lookup(s.name, v))
            fail(file, s.loc, "`" + s.name + "` is not declared",
                 "clean rejects unknown names instead of emitting Lean that fails later (D5).",
                 "declare it first: `char " + s.name + "[] = \"\";`.");
        if (v.type.kind != Type::Kind::String)
            fail(file, s.loc, "`fgets` fills `" + s.name + "`, which is a `" + cName(v.type) + "`",
                 "a line is a Lean `String`; arrays of characters are not translated.",
                 "declare it as a string: `char " + s.name + "[] = \"\";`.");
        helpers.insert("fgets");
        markAssigned(s.name, s.loc);
    }

    void stmts(std::vector<Stmt>& list) {
        bool dead = false;
        bool deadByExit = false;  // what ended the block: `exit(…)`, or `return` (and the like)
        for (Stmt& s : list) {
            if (dead && deadByExit)
                fail(file, s.loc, "unreachable code after `exit`",
                     "the program ends at `exit`, so this code can never run.",
                     "delete it, or move the `exit` below it.");
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
            case Stmt::Kind::Assign:
                assignStmt(s);
                break;
            case Stmt::Kind::CallStmt:
                callStmt(s);
                break;
            case Stmt::Kind::If:
                cond(s.expr);
                scoped(s.body);
                scoped(s.elseBody);
                break;
            case Stmt::Kind::While:
                cond(s.expr);
                scoped(s.body);
                break;
            case Stmt::Kind::DoWhile: {
                // the condition is re-tested at the end of every iteration, inside
                // the body's do-block; a body local shadowing one of its names
                // would change what it reads (C keeps the outer binding)
                InjectScan scan;
                freeIdents(s.expr, scan.names);
                scan.walk(s.body, {}, false, true, s.loc);
                if (scan.bad)
                    fail(file, scan.loc,
                         "`" + scan.which + "` is redeclared where the `do … while` condition is re-tested",
                         "the condition lands at the end of the body, inside the scope of the "
                         "redeclared `" + scan.which + "`, so it would read the local instead "
                         "of the variable C sees.",
                         "rename the local.");
                scoped(s.body);
                cond(s.expr);
                break;
            }
            case Stmt::Kind::For:
                forStmt(s);
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
            case Stmt::Kind::Continue:
                break;  // `break`/`continue` legality is the parser's checkJumps
            case Stmt::Kind::Print:
                printStmt(s);
                break;
            case Stmt::Kind::Scan:
                scanStmt(s);
                break;
            case Stmt::Kind::ReadLine:
                readLineStmt(s);
                break;
            case Stmt::Kind::Close:
                valueAs(s.stream, Type::of(Type::Kind::File));
                break;
            case Stmt::Kind::Exit:  // Lean's `IO.Process.exit` takes a `UInt8`: C's "status mod 256"
                valueAs(s.expr, Type::of(Type::Kind::UInt, 8));
                break;
            }
            if (stmtReturns(s)) {
                dead = true;
                deadByExit = s.kind == Stmt::Kind::Exit;
            }
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
    for (const Function& f : unit.functions)
        if (isLibraryName(f.name))
            fail(unit.fileName, Loc{f.defLine, 1}, "`" + f.name + "` is defined in this file",
                 "clean translates calls to `" + f.name + "` as the C library function (main-spec §8.1), "
                 "so a function of that name would never be called.",
                 "rename the function.");
    prepareMain(unit);  // `int main` → `IO UInt32`, before the signatures are taken
    findIO(unit);       // which functions are `IO`
    std::map<std::string, Sig> sigs;
    for (const Function& f : unit.functions) {
        if (sigs.count(f.name))
            fail(unit.fileName, Loc{f.defLine, 1}, "function `" + f.name + "` is defined twice",
                 "Lean allows only one definition per name.", "rename one of them.");
        Sig sig;
        sig.ret = f.retType;
        sig.io = f.isIO;
        for (const auto& p : f.params) sig.params.push_back(p.first);
        sigs[f.name] = sig;
    }
    findWriters(unit, unit.fileName);  // OD11 (c): sets writerParam, refuses what cannot be expressed
    std::map<std::string, int> writers;  // name → its written array parameter's index
    for (const Function& f : unit.functions)
        if (f.writerParam >= 0) writers[f.name] = f.writerParam;
    for (Function& f : unit.functions)
        Checker{unit.fileName, sigs, f, unit.pragmas, unit.notes, writers, unit.helpers, {}, {}}.run();

    // File-scope `printf` etc. (main-spec §8.2): checked like the body of a `void` function with
    // no parameters, so names, types and arities get the same treatment.
    if (!unit.scripts.empty()) {
        Function script;
        script.name = "<file scope>";
        script.retType = Type::of(Type::Kind::Unit);
        script.body = std::move(unit.scripts);
        Checker{unit.fileName, sigs, script, unit.pragmas, unit.notes, writers, unit.helpers, {}, {}}.run();
        unit.scripts = std::move(script.body);
    }

    for (const std::string& h : helperClosure(unit.helpers))  // a generated helper's name must be free
        for (const Function& f : unit.functions)
            if (f.name == h)
                fail(unit.fileName, Loc{f.defLine, 1},
                     "`" + h + "` is defined, and the translation needs a helper of that name",
                     "clean writes `def " + h + "` into the output (see the top of the generated file).",
                     "rename your function.");
}
