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
#include <functional>
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
        bool hasDefault = s.exhaustive;  // every enumerator has a case: Lean's `match` is total
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
                const bool element = !s.indices.empty() || (!s.path.empty() && !s.path[0].field);
                if (element && p >= 0) noteWrite((size_t)p, s.loc);
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
        if (f.isExtern) continue;  // a value-level extern is pure (main-spec §7.2.1)
        f.isIO = f.name == "main" || hasPrint(f.body);
        if (f.isIO) io.insert(f.name);
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (Function& f : unit.functions) {
            if (f.isIO || f.isExtern) continue;
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
    bool io = false;      // the function does I/O (findIO)
    bool global = false;  // a `const` global (main-spec §7.3), not a function
};

// An enumerator (main-spec §4.5): its enum type and C value.
struct EnumConst {
    Type type;
    long long value = 0;
};

// A visible name: its type, and its Decl (nullptr for a parameter) so
// assignments can mark the Decl `mut`.
struct Var {
    Stmt* decl;
    Type type;
    bool global = false;  // a `const` global: read-only
};

struct Checker {
    const std::string& file;
    const std::map<std::string, Sig>& sigs;  // function name → parameter types and return type
    Function& fn;
    const std::set<std::string>& pragmas;    // `#pragma lean …` words
    std::vector<DiagError>& notes;           // hints (H…) the unit carries to main
    const std::map<std::string, int>& writers;  // name → written array parameter's index (OD11 c)
    std::set<std::string>& helpers;             // the generated helpers this file needs (prelude.h)
    const std::map<std::string, EnumConst>& enumConsts;  // every enumerator of the file (main-spec §4.5)
    const std::map<std::string, const TypeDecl*>& structs;  // every struct of the file, by name
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

    // The field `name` of struct type `st` (refused when there is none).
    const Field& fieldOf(Type st, const std::string& name, Loc loc) {
        if (st.kind != Type::Kind::Struct)
            fail(file, loc, "`." + name + "` on a `" + cName(st) + "`", "only structs have fields.",
                 "read the field of a struct value.");
        const TypeDecl& d = *structs.at(st.name);
        for (const Field& f : d.fields)
            if (f.name == name) return f;
        fail(file, loc, "`" + cName(st) + "` has no field `" + name + "`",
             "clean rejects unknown names instead of emitting Lean that fails later (D5).", "fix the field name.");
    }

    // `{1, 2}` / `{.x = 1, .y = 2}` as a value of struct type `st` (main-spec §4.5). Missing
    // fields are C's zero; the emitter writes them out.
    void structInit(Expr& lit, Type st) {
        const TypeDecl& d = *structs.at(st.name);
        lit.type = st;
        if (lit.fields.empty() && lit.kids.size() > d.fields.size())
            fail(file, lit.loc, "too many initialisers for `" + cName(st) + "`",
                 "C rejects it too.", "drop the extra values.");
        std::set<std::string> seen;
        for (size_t k = 0; k < lit.kids.size(); ++k) {
            const Field& f = lit.fields.empty() ? d.fields[k] : fieldOf(st, lit.fields[k], lit.kids[k].loc);
            if (!seen.insert(f.name).second)
                fail(file, lit.kids[k].loc, "field `" + f.name + "` is initialised twice",
                     "C would keep the last one; clean does not pick.", "keep one.");
            initValue(lit.kids[k], f.type, f.dims);
        }
    }

    // One element of an initialiser list: a nested list for an array or a struct, else a value.
    void initValue(Expr& kid, Type t, std::vector<long long> dims) {
        if (kid.kind == Expr::Kind::ArrayLit && t.kind == Type::Kind::Struct) {
            structInit(kid, t);
        } else if (kid.kind == Expr::Kind::ArrayLit && t.kind == Type::Kind::Array) {
            initList(kid, t, dims, 0);
        } else {
            if (kid.kind == Expr::Kind::ArrayLit)
                fail(file, kid.loc, "a `{ … }` list for a `" + cName(t) + "`",
                     "C would accept braces around a scalar; Lean has no such thing.", "drop the braces.");
            valueAs(kid, t);
        }
    }

    Type valueType(Expr& e) {
        switch (e.kind) {
        case Expr::Kind::Raw:  // `LEAN_EXPR("…")` has no C type of its own: only `valueAs` / a cast give it one
            fail(file, e.loc, "`LEAN_EXPR(…)` is used where clean cannot tell its type",
                 "the Lean text is passed through unread, so its type has to come from where it stands.",
                 "store it first (`int x = LEAN_EXPR(\"…\");`), pass it as an argument, return it, or cast it: "
                 "`(int)LEAN_EXPR(\"…\")`.");
        case Expr::Kind::Member: {  // `p.x` (main-spec §4.5)
            Type st = value(e.kids[0]);
            return fieldOf(st, e.text, e.loc).type;
        }
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
            auto en = enumConsts.find(e.text);  // `RED`: the constructor, in scope through `open Color`
            if (en != enumConsts.end()) return en->second.type;
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
            if (kid.kind == Expr::Kind::Raw) {  // `(T)LEAN_EXPR("…")`: the cast names its type
                kid.type = e.type;
                return e.type;
            }
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
            if (classifyLibName(e.text) == LibName::BitVec) return bitVecHelper(e);
            if (stdShape(e.text) != StdShape::None && !sigs.count(e.text)) return stdCall(e);
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
            if (it != sigs.end() && it->second.global)
                fail(file, e.loc, "the constant `" + e.text + "` is called like a function",
                     "it is a value, not a function.", "drop the `(…)`.");
            if (it == sigs.end() && (e.text == "malloc" || e.text == "calloc" || e.text == "realloc" || e.text == "free"))
                failDiag("E108", e.loc, "dynamic memory (`" + e.text + "`) is not translatable",
                         "Lean values are immutable; there is no heap model (main-spec §12.2).",
                         "declare an array of fixed size (`int a[8];`) and index it.");
            if (it == sigs.end() && (e.text == "setjmp" || e.text == "longjmp"))
                failDiag("E109", e.loc, "`" + e.text + "` is not translatable",
                         "a non-local jump has no Lean counterpart.", "return an error value instead.");
            if (it == sigs.end())
                fail(file, e.loc, "`" + e.text + "` is called but not defined in this file",
                     "calls are resolved inside the file (D5: an unknown name is not guessed).",
                     "define `" + e.text + "` in the same file, or declare it `extern` if Lean defines it (§7.2.1).");
            if (it->second.params.size() != e.kids.size())
                fail(file, e.loc, "`" + e.text + "` takes " + std::to_string(it->second.params.size()) +
                         " argument(s) but is called with " + std::to_string(e.kids.size()),
                     "the arity is checked here so that Lean does not have to report it.",
                     "match the parameter list.");
            for (size_t k = 0; k < e.kids.size(); ++k) valueAs(e.kids[k], it->second.params[k]);
            e.io = it->second.io;
            return it->second.ret;
        }
        case Expr::Kind::Cond: {  // `c ? a : b` → `if c then a else b`: both branches meet at one type (D19)
            cond(e.kids[0]);
            Type result = Type::of(Type::Kind::Bool);
            if (isTestOp(e.kids[1]) || isTestOp(e.kids[2])) {  // `c ? a < b : p`: a `bool` either way
                valueAs(e.kids[1], result);
                valueAs(e.kids[2], result);
            } else {
                Type a = value(e.kids[1]);
                Type b = value(e.kids[2]);
                Unified u = unify(a, b, true);  // equal types meet as they are (two chars stay `Char`, as in a comparison)
                if (!u.ok) failDiag(u.code, e.loc, u.problem, u.why, u.fix);
                coerce(e.kids[1], u.type);
                coerce(e.kids[2], u.type);
                result = u.type;
            }
            for (int k = 1; k <= 2; ++k)
                if (callsIO(e.kids[k]))
                    fail(file, e.kids[k].loc, "an I/O call inside a branch of `?:`",
                         "C runs only the branch it picks; Lean's `(← f x)` would run before the `if`, "
                         "whichever branch is taken.",
                         "use an `if`/`else` statement, or call it first and keep the result.");
            return result;
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

    // ---- the pure C library (main-spec §12) ----------------------------------------------

    Type stdCall(Expr& e) {
        const StdShape shape = stdShape(e.text);
        const size_t want = shape == StdShape::Math2 || shape == StdShape::MinMax || shape == StdShape::StrCmp ||
                                    shape == StdShape::StrCat ? 2 : 1;
        if (e.kids.size() != want)
            fail(file, e.loc, "`" + e.text + "` takes " + std::to_string(want) + " argument(s) but is called with " +
                     std::to_string(e.kids.size()),
                 "the arity is checked here so that Lean does not have to report it.", "match the C library's signature.");
        e.lib = true;
        const Type dbl = Type::of(Type::Kind::Float64), flt = Type::of(Type::Kind::Float32);
        const Type str = Type::of(Type::Kind::String), chr = Type::of(Type::Kind::Char);
        switch (shape) {
        case StdShape::Math1:
        case StdShape::Math2: {
            const Type t = stdFloat32(e.text) ? flt : dbl;
            for (Expr& k : e.kids) valueAs(k, t);  // `sqrt(n)` with an `int` n: `Float.sqrt (Float.ofInt n)` (D19)
            return t;
        }
        case StdShape::FloatTest: {
            const Type t = value(e.kids[0]);
            valueAs(e.kids[0], t.kind == Type::Kind::Float32 ? flt : dbl);
            return Type::of(Type::Kind::Bool);
        }
        case StdShape::Abs:  // C's `int abs(int)`: `(↑(Int.natAbs x) : Int)`
            valueAs(e.kids[0], Type::of(Type::Kind::Int));
            return Type::of(Type::Kind::Int);
        case StdShape::MinMax: {
            Type a = value(e.kids[0]), b = value(e.kids[1]);
            Unified u = unify(a, b);
            if (!u.ok) failDiag(u.code, e.loc, u.problem, u.why, u.fix);
            if (u.type.kind == Type::Kind::Literal) u.type = Type::of(Type::Kind::Int);
            if (u.type.kind == Type::Kind::FloatLiteral) u.type = dbl;
            OpRule r = checkCompare("<", u.type);
            if (!r.ok) failDiag(r.code, e.loc, r.problem, r.why, r.fix);
            coerce(e.kids[0], u.type);
            coerce(e.kids[1], u.type);
            return u.type;
        }
        case StdShape::StrLen:
            valueAs(e.kids[0], str);
            return Type::of(Type::Kind::Nat);
        case StdShape::StrCmp:
            valueAs(e.kids[0], str);
            valueAs(e.kids[1], str);
            helpers.insert("strcmp");
            return Type::of(Type::Kind::Int);
        case StdShape::CharTest:
        case StdShape::CharMap:
            valueAs(e.kids[0], chr);  // C passes an `int`; Lean's tests take a `Char` (W007 for the conversion)
            return shape == StdShape::CharTest ? Type::of(Type::Kind::Bool) : chr;
        case StdShape::StrCat:
        case StdShape::Assert:
            fail(file, e.loc, "the result of `" + e.text + "(…)` is used as a value",
                 shape == StdShape::StrCat ? "`strcat` changes its first argument; Lean strings are values, so it is "
                                             "translated as the statement `dst := dst ++ src`."
                                           : "`assert` is a statement (`assert! c`).",
                 "call it as a statement, on its own line.");
        case StdShape::None:
            break;
        }
        return Type::of(Type::Kind::Unit);  // unreachable
    }

    // ---- the bit-vector helpers (main-spec §5.4) ---------------------------------------

    // A constant argument (a width, a bit position): a non-negative integer literal.
    long long constArg(Expr& e, const std::string& fn, const std::string& what) {
        if (e.kind != Expr::Kind::IntLit)
            failDiag("E302", e.loc, "the " + what + " of `" + fn + "` is not a constant",
                     "the result's width is part of its Lean type (`BitVec N`), so it is needed while translating.",
                     "write a literal, or a `#define NAME <integer>`.");
        e.type = Type::of(Type::Kind::Nat);
        return labelValue(e);
    }

    // An argument that must be a bit-vector (its width is not guessed from a literal).
    Type bvArg(Expr& e, const std::string& fn) {
        Type t = value(e);
        if (t.kind != Type::Kind::BitVec)
            fail(file, e.loc, "`" + fn + "` takes a `bv(N)`, not a `" + cName(t) + "`",
                 "the helpers of main-spec §5.4 work on bit-vectors.",
                 isLiteral(t) ? "give the literal its width: `BV(8, …)`." : "cast it: `(bv(8))x`.");
        return t;
    }

    Type bitVecHelper(Expr& e) {
        static const std::map<std::string, size_t> arity = {
            {"bv_cat", 2}, {"bv_ext", 3}, {"bv_bit", 2}, {"bv_zext", 2}, {"bv_sext", 2}, {"bv_rotl", 2},
            {"bv_rotr", 2}, {"bv_slt", 2}, {"bv_sle", 2}, {"bv_ult", 2}, {"bv_ule", 2}, {"bv_sdiv", 2},
            {"bv_sshr", 2}};
        const std::string& fn = e.text;
        if (e.kids.size() != arity.at(fn))
            fail(file, e.loc, "`" + fn + "` takes " + std::to_string(arity.at(fn)) + " argument(s) but is called with " +
                     std::to_string(e.kids.size()),
                 "the arity is checked here so that Lean does not have to report it.", "see main-spec §5.4.");
        const Type nat = Type::of(Type::Kind::Nat);
        if (fn == "bv_cat") {  // widths add
            const Type a = bvArg(e.kids[0], fn), b = bvArg(e.kids[1], fn);
            return Type::of(Type::Kind::BitVec, a.width + b.width);
        }
        if (fn == "bv_ext") {  // bits hi..lo
            const Type x = bvArg(e.kids[0], fn);
            const long long hi = constArg(e.kids[1], fn, "high bit"), lo = constArg(e.kids[2], fn, "low bit");
            if (lo > hi || hi >= x.width)
                failDiag("E301", e.loc, "`bv_ext(x, " + std::to_string(hi) + ", " + std::to_string(lo) + ")` on a `" +
                             cName(x) + "`",
                         "the bits must satisfy lo ≤ hi < width.", "pick bits inside the value.");
            return Type::of(Type::Kind::BitVec, (int)(hi - lo + 1));
        }
        if (fn == "bv_zext" || fn == "bv_sext") {  // to width N
            const long long n = constArg(e.kids[0], fn, "width");
            bvArg(e.kids[1], fn);
            if (n < 1) failDiag("E302", e.kids[0].loc, "a width of 0", "a bit-vector needs at least one bit.", "write 1 or more.");
            return Type::of(Type::Kind::BitVec, (int)n);
        }
        if (fn == "bv_bit") {
            bvArg(e.kids[0], fn);
            valueAs(e.kids[1], nat);
            return Type::of(Type::Kind::Bool);
        }
        if (fn == "bv_rotl" || fn == "bv_rotr" || fn == "bv_sshr") {  // the count is a `Nat`
            const Type x = bvArg(e.kids[0], fn);
            valueAs(e.kids[1], nat);
            return x;
        }
        // bv_slt bv_sle bv_ult bv_ule bv_sdiv: two operands of one width (a literal adapts)
        Type a = value(e.kids[0]), b = value(e.kids[1]);
        if (a.kind != Type::Kind::BitVec && b.kind != Type::Kind::BitVec) bvArg(e.kids[0], fn);
        Unified u = unify(a, b);
        if (!u.ok) failDiag(u.code, e.loc, u.problem, u.why, u.fix);
        if (u.type.kind != Type::Kind::BitVec) bvArg(e.kids[0], fn);
        coerce(e.kids[0], u.type);
        coerce(e.kids[1], u.type);
        return fn == "bv_sdiv" ? u.type : Type::of(Type::Kind::Bool);
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
        if (e.kind == Expr::Kind::Raw) {  // `LEAN_EXPR("…")` takes the type expected here (§11.1)
            e.type = target;
            return;
        }
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
        if (v.global)
            fail(file, loc, "assignment to the constant `" + name + "`",
                 "a `const` global is a Lean `def`, which never changes (C rejects it too).",
                 "copy it into a local and change that.");
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
            for (PathStep& st : s.path) {  // `p.a[i].x = v`: walk down through fields and elements
                if (st.field) {
                    t = fieldOf(t, st.name, s.loc).type;
                    continue;
                }
                if (t.kind == Type::Kind::String)
                    failCode("E101", file, s.loc, "mutation of a string by index",
                             "Lean strings are immutable (main-spec §4.3).",
                             "use an `Array Char`, or `uint8_t`/`bv(N)` arrays for byte buffers.");
                if (t.kind != Type::Kind::Array)
                    fail(file, s.loc, "indexing a `" + cName(t) + "` inside `" + s.name + "`",
                         "only arrays can be indexed.", "index an array.");
                valueAs(st.index, Type::of(Type::Kind::Nat));
                t = t.elem();
            }
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
            const bool elementWrite = !s.indices.empty() || (!s.path.empty() && !s.path[0].field);
            if (elementWrite && !target.decl && target.type.kind == Type::Kind::Array &&
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
            if ((s.path.empty() ? s.indices.empty() : s.path.back().field) && t.kind == Type::Kind::Array)
                fail(file, s.loc, "assignment to the whole array `" + s.name + "`",
                     "C rejects it too.", "assign the elements one by one.");
            valueAs(s.expr, t);
        }
        markAssigned(s.name, s.loc);  // refuses unknown names and functions
    }

    // `f(args);` — the CallStmt case of `stmts`, also a `for` header part.
    void callStmt(Stmt& s) {
        const StdShape shape = sigs.count(s.expr.text) ? StdShape::None : stdShape(s.expr.text);
        if (shape == StdShape::Assert || shape == StdShape::StrCat) {  // main-spec §12.3, §12.5
            Expr& e = s.expr;
            if (e.kids.size() != (shape == StdShape::Assert ? 1u : 2u))
                fail(file, e.loc, "`" + e.text + "` is called with the wrong number of arguments",
                     "the arity is checked here so that Lean does not have to report it.",
                     "match the C library's signature.");
            e.lib = true;
            e.type = Type::of(Type::Kind::Unit);
            if (shape == StdShape::Assert) {  // `assert! c`: a run-time check, not a proof obligation
                cond(e.kids[0]);
                noIO(e.kids[0]);
                return;
            }
            if (e.kids[0].kind != Expr::Kind::Ident)
                fail(file, e.kids[0].loc, "`strcat` appends to something that is not a variable",
                     "the new string is assigned back, so it must be a whole variable.", "append to a string variable.");
            valueAs(e.kids[0], Type::of(Type::Kind::String));
            valueAs(e.kids[1], Type::of(Type::Kind::String));
            markAssigned(e.kids[0].text, e.kids[0].loc);
            s.writeBack = e.kids[0].text;  // `dst := dst ++ src`
            return;
        }
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
                  s.init[0].dims.empty() && !s.init[0].noInit &&
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
        const bool onEnum = st.kind == Type::Kind::Enum;
        if (!onEnum && st.kind != Type::Kind::Int && st.kind != Type::Kind::Nat && st.kind != Type::Kind::Literal)
            fail(file, s.expr.loc, "`switch` on a `" + cName(st) + "` is not translated yet",
                 "only `int`/`unsigned` and `enum` scrutinees are; match patterns on the other types are unverified.",
                 "use an `if`/`else if` chain.");
        // labels: on an enum, its enumerators (Lean constructor patterns); on an integer, numbers —
        // an enumerator there stands for its value, as in C
        std::set<std::string> seenNames;
        for (Case& c : s.cases)
            for (Expr& lab : c.labels) {
                if (lab.kind != Expr::Kind::Ident) {
                    if (onEnum)
                        fail(file, lab.loc, "a number labels a case of a `switch` on `" + cName(st) + "`",
                             "the scrutinee is a Lean `inductive`; its cases are the enumerators.",
                             "write the enumerator.");
                    continue;
                }
                auto en = enumConsts.find(lab.text);
                if (en == enumConsts.end())
                    fail(file, lab.loc, "`case " + lab.text + ":` — `" + lab.text + "` is not an enumerator",
                         "a label is an integer literal or an enumerator; macros are not expanded.",
                         "write the number.");
                if (onEnum) {
                    if (!(en->second.type == st))
                        fail(file, lab.loc, "`" + lab.text + "` belongs to `" + cName(en->second.type) +
                                 "`, the switch is on `" + cName(st) + "`",
                             "a Lean `match` pattern must have the scrutinee's type.", "use an enumerator of `" + cName(st) + "`.");
                    if (!seenNames.insert(lab.text).second)
                        fail(file, lab.loc, "duplicate `case` value", "C rejects it too.",
                             "remove or change one of the labels.");
                    lab.type = st;
                } else {
                    const Loc at = lab.loc;
                    lab = Expr();
                    lab.kind = Expr::Kind::IntLit;
                    lab.text = std::to_string(en->second.value);
                    lab.loc = at;
                }
            }
        if (onEnum) {  // every enumerator has a case: the `match` is total without `| _`
            size_t total = 0;
            for (const auto& en : enumConsts) total += en.second.type == st;
            s.exhaustive = seenNames.size() == total;
        }
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
                if (!onEnum && !seen.insert(labelValue(lab)).second)
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
            } else if (elem.kind == Type::Kind::Struct && kid.kind == Expr::Kind::ArrayLit) {
                structInit(kid, elem);  // `struct P ps[2] = {{1, 2}, {3, 4}};`
            } else {
                if (kid.kind == Expr::Kind::ArrayLit)
                    fail(file, kid.loc, "a nested `{ … }` for a one-dimensional array",
                         "C would accept braces around a scalar; Lean has no such thing.", "drop the inner braces.");
                valueAs(kid, elem);
            }
        }
    }

    void declStmt(Stmt& s) {
        if (s.type.kind == Type::Kind::Struct && !s.noInit && s.expr.kind == Expr::Kind::ArrayLit) {
            structInit(s.expr, s.type);  // `struct P p = {1, 2};`
            return;
        }
        if (s.type.kind != Type::Kind::Array) {
            if (!s.noInit) valueAs(s.expr, s.type);  // `int x;` starts at zero (§4.5, the emitter writes it)
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
                case Type::Kind::Enum:
                    coerce(a, nat);  // C prints the character code / the enumerator's value
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
        for (size_t k = 0; k < list.size(); ++k) {
            Stmt& s = list[k];
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
            case Stmt::Kind::Raw:  // `LEAN_STMT("…")`: Lean's to judge (main-spec §11.1)
                break;
            case Stmt::Kind::Exit:  // Lean's `IO.Process.exit` takes a `UInt8`: C's "status mod 256"
                valueAs(s.expr, Type::of(Type::Kind::UInt, 8));
                break;
            }
            if (stmtReturns(s)) {
                if (s.kind == Stmt::Kind::Switch && s.exhaustive) {
                    // main-spec §14.3: a `switch` with a case for every enumerator returns on its
                    // own; C's safety-net `return 0;` after it is unreachable in Lean, so it is dropped
                    list.erase(list.begin() + (long)k + 1, list.end());
                    return;
                }
                dead = true;
                deadByExit = s.kind == Stmt::Kind::Exit;
            }
        }
    }

    void run() {
        scopes.emplace_back();  // the outermost scope: the file's `const` globals (main-spec §7.3)
        bare.push_back(false);
        for (const auto& g : sigs)
            if (g.second.global && g.first != fn.name) scopes[0][g.first] = Var{nullptr, g.second.ret, true};
        if (fn.isGlobal) {  // `const T name = e;`: the initialiser is checked like a local's
            Stmt& decl = fn.body[0];
            declStmt(decl);
            bool io = false;
            forEachExpr(fn.body, [&](const Expr& e) { io = io || callsIO(e); });
            if (io)
                fail(file, decl.loc, "the constant `" + fn.name + "` is computed with I/O",
                     "a Lean `def` is a pure value; it cannot read or print.",
                     "compute it inside `main`, or call a pure function.");
            return;
        }
        scopes.emplace_back();
        bare.push_back(false);
        std::set<std::string> seen;
        for (const auto& p : fn.params) {
            if (!seen.insert(p.second).second)
                fail(file, Loc{fn.defLine, 1}, "parameter `" + p.second + "` is declared twice",
                     "C rejects it too.", "rename one of them.");
            scopes[1][p.second] = Var{nullptr, p.first};
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
    std::map<std::string, const TypeDecl*> structs;  // every struct, by name (main-spec §4.5)
    std::set<std::string> typeNames;
    for (const TypeDecl& d : unit.typeDecls) {
        if (!typeNames.insert(d.name).second)
            fail(unit.fileName, d.loc, "`" + d.name + "` names two types",
                 "C keeps `struct`, `enum` and `typedef` names apart; Lean has one namespace for them.",
                 "rename one of them.");
        if (d.kind == TypeDecl::Kind::Struct) structs[d.name] = &d;
    }
    std::map<std::string, EnumConst> enumConsts;  // every enumerator, by name (main-spec §4.5)
    for (const TypeDecl& d : unit.typeDecls) {
        if (d.kind != TypeDecl::Kind::Enum) continue;
        for (const Enumerator& en : d.enumerators) {
            if (enumConsts.count(en.name))
                fail(unit.fileName, en.loc, "enumerator `" + en.name + "` is defined twice",
                     "C rejects it too, and `open` would make the name ambiguous in Lean.", "rename one of them.");
            enumConsts[en.name] = EnumConst{Type::named(Type::Kind::Enum, d.name), en.value};
            for (const Function& f : unit.functions)
                if (f.name == en.name)
                    fail(unit.fileName, Loc{f.defLine, 1}, "`" + f.name + "` is both an enumerator and a function",
                         "C rejects it too: they share one namespace.", "rename one of them.");
        }
    }
    for (const TypeDecl& d : unit.typeDecls)  // a Lean type and a Lean function cannot share a name
        for (const Function& f : unit.functions)
            if (f.name == d.name)
                fail(unit.fileName, Loc{f.defLine, 1}, "`" + f.name + "` names both a type and a function",
                     "Lean has one namespace for both, so the two definitions would clash.",
                     "rename one of them.");
    prepareMain(unit);  // `int main` → `IO UInt32`, before the signatures are taken
    findIO(unit);       // which functions are `IO`
    std::map<std::string, Sig> sigs;
    for (const Function& f : unit.functions) {
        if (f.isExtern && f.name == "main")
            fail(unit.fileName, Loc{f.defLine, 1}, "`extern main`", "`main` is the program; it is defined here.",
                 "drop `extern`.");
        if (sigs.count(f.name))
            fail(unit.fileName, Loc{f.defLine, 1}, "function `" + f.name + "` is defined twice",
                 "Lean allows only one definition per name.", "rename one of them.");
        Sig sig;
        sig.ret = f.retType;
        sig.io = f.isIO;
        sig.global = f.isGlobal;
        for (const auto& p : f.params) sig.params.push_back(p.first);
        sigs[f.name] = sig;
    }
    findWriters(unit, unit.fileName);  // OD11 (c): sets writerParam, refuses what cannot be expressed
    std::map<std::string, int> writers;  // name → its written array parameter's index
    for (const Function& f : unit.functions)
        if (f.writerParam >= 0) writers[f.name] = f.writerParam;
    for (Function& f : unit.functions) {
        if (f.isExtern) {  // defined in Lean: only its signature matters (main-spec §7.2.1)
            if (f.retType.kind == Type::Kind::Unit)
                fail(unit.fileName, Loc{f.defLine, 1}, "`extern void " + f.name + "(…)`",
                     "a value-level `extern` is a pure Lean function, and a pure function returning "
                     "`Unit` does nothing.",
                     "give it a return type, or define it in C.");
            for (const auto& p : f.params)
                if (p.first.kind == Type::Kind::File)
                    fail(unit.fileName, Loc{f.defLine, 1}, "`extern " + f.name + "` takes a `FILE *`",
                         "a value-level `extern` is pure; streams need `IO`.", "define it in C.");
            continue;
        }
        Checker{unit.fileName, sigs, f, unit.pragmas, unit.notes, writers, unit.helpers, enumConsts, structs, {}, {}}.run();
    }

    // File-scope `printf` etc. (main-spec §8.2): checked like the body of a `void` function with
    // no parameters, so names, types and arities get the same treatment.
    if (!unit.scripts.empty()) {
        Function script;
        script.name = "<file scope>";
        script.retType = Type::of(Type::Kind::Unit);
        script.body = std::move(unit.scripts);
        Checker{unit.fileName, sigs, script, unit.pragmas, unit.notes, writers, unit.helpers, enumConsts, structs, {}, {}}.run();
        unit.scripts = std::move(script.body);
    }

    // VERIFY_FORALL (main-spec §9.3): the body is checked as `bool f(binders) { return body; }`.
    std::set<std::string> theoremNames;
    for (Verify& v : unit.verifies) {
        int bits = 0;
        for (const auto& b : v.binders) {
            const Type& t = b.first;
            if (t.kind != Type::Kind::BitVec && t.kind != Type::Kind::UInt && t.kind != Type::Kind::SInt &&
                t.kind != Type::Kind::Bool)
                fail(unit.fileName, v.loc, "`VERIFY_FORALL` over a `" + cName(t) + "` (`" + b.second + "`)",
                     "the theorem is proved by `bv_decide`, a SAT search over finitely many bits; `" + cName(t) +
                         "` has no fixed width.",
                     "quantify over `bv(N)`, `uintN_t`/`intN_t` or `bool`.");
            bits += t.kind == Type::Kind::Bool ? 1 : t.width;
        }
        Function f;
        f.name = "<VERIFY_FORALL>";
        f.defLine = v.line;
        f.retType = Type::of(Type::Kind::Bool);
        f.params = v.binders;
        Stmt ret;
        ret.kind = Stmt::Kind::Return;
        ret.loc = v.body.loc;
        ret.expr = std::move(v.body);
        f.body.push_back(std::move(ret));
        Checker{unit.fileName, sigs, f, unit.pragmas, unit.notes, writers, unit.helpers, enumConsts, structs, {}, {}}.run();
        v.body = std::move(f.body[0].expr);
        bool io = false;
        std::function<void(const Expr&)> scan = [&](const Expr& e) {
            io = io || (e.kind == Expr::Kind::Call && e.io);
            for (const Expr& kid : e.kids) scan(kid);
        };
        scan(v.body);
        if (io)
            fail(unit.fileName, v.loc, "`VERIFY_FORALL` calls an I/O function",
                 "a theorem is about values; an `IO` action has none to compare.", "verify the pure functions.");
        if (!v.named) {  // `f(…) == g(…)` → `f_eq_g`, else `verify_LINE`
            const Expr& b = v.body;
            const bool eq = b.kind == Expr::Kind::Cmp && b.text == "==";
            auto callee = [](const Expr& e) {
                const Expr* p = &e;
                while (p->kind == Expr::Kind::Cast && p->implicit) p = &p->kids[0];
                return p->kind == Expr::Kind::Call && !p->lib ? p->text : std::string();
            };
            const std::string l = eq ? callee(b.kids[0]) : "", r = eq ? callee(b.kids[1]) : "";
            v.name = !l.empty() && !r.empty() && l != r ? l + "_eq_" + r : "verify_" + std::to_string(v.line);
        }
        if (sigs.count(v.name) || !theoremNames.insert(v.name).second)
            fail(unit.fileName, v.loc, "the theorem name `" + v.name + "` is already taken",
                 "Lean allows one declaration per name.", "name the theorem with a label: `my_name: VERIFY_FORALL(…)`.");
        if (bits > 64)  // W012: shown whenever the feature is used (main-spec §13.1)
            unit.notes.push_back(DiagError(
                "W012", unit.fileName, v.loc,
                "`VERIFY_FORALL` ranges over " + std::to_string(bits) + " bits",
                "`bv_decide` searches all of them; above 64 the SAT problem can take very long.",
                "split the check, or narrow the variables."));
    }

    for (const std::string& h : helperClosure(unit.helpers))  // a generated helper's name must be free
        for (const Function& f : unit.functions)
            if (f.name == h)
                fail(unit.fileName, Loc{f.defLine, 1},
                     "`" + h + "` is defined, and the translation needs a helper of that name",
                     "clean writes `def " + h + "` into the output (see the top of the generated file).",
                     "rename your function.");
}
