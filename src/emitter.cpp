// emitter.cpp — implementation of emitter.h.
//
// Prints the AST as Lean. The decisions that live here:
//   * parentheses: C and Lean bind + - * / % identically, so a child needs
//     parentheses only when it binds looser than its parent (or equally
//     loose on the right, which keeps the tree's shape explicit);
//   * parameters of the same type merge into one Lean group:
//     `int a, int b` → `(a b : Int)` (main-spec §7.1); type names come from
//     types.h, never from here;
//   * a body that is one `return e;` becomes `def f … := e`; any other body
//     becomes `Id.run do` with `let` / `let mut` / `:=` / `if` / `match`;
//   * tests are Lean propositions: `==` → `=`, `!=` → `≠`, `&&` → `∧`,
//     `||` → `∨`, `!` → `¬`, and a bare number `x` in a test means `x ≠ 0`;
//   * a bare `{ … }` block is flattened into its parent (Lean's nested `do` is not a scope);
//   * recursive functions are printed as they are; mutually recursive ones
//     share a `mutual … end` block (set up by toposort.cpp);
//   * a `void` writer (OD11 c, decision D20) returns the array parameter it
//     wrote instead of `Unit`: `return;` and every path that falls off the
//     end become `return a`, a value-yielding `if` gets a missing-`else`
//     `return a` arm, and a writer call statement re-assigns the variable
//     that was passed (`fill(x, n);` → `x := fill x n`);
//   * comments travel with the tree and are printed at the nearest legal
//     spot: `//` → `--`, `/* */` → `/- -/`, above the statement they belong
//     to; the ones that closed a block or a file land there (see parser.h
//     for the attachment rule);

#include "emitter.h"

#include <vector>

#include "types.h"

namespace {

std::string trimBlank(const std::string& s) {  // small twin of the lexer's; not worth sharing
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
    return s.substr(b, e - b);
}

std::string pad(int level) { return std::string(2 * level, ' '); }

// ---- values ---------------------------------------------------------------

// How tightly a Lean spelling binds; higher binds tighter. Only used to place
// parentheses. The numbers are Lean's own operator levels, which differ from
// C's (`+` binds looser than `<<<` in Lean, tighter in C), so the C tree is
// printed with the parentheses Lean needs, not the ones C would.
int leanLevel(const std::string& op) {
    if (op == "|") return 55;                  // |||
    if (op == "^") return 58;                  // ^^^
    if (op == "&") return 60;                  // &&&
    if (op == "+" || op == "-") return 65;
    if (op == "*" || op == "/" || op == "%") return 70;
    return 75;                                 // <<<  >>>
}

std::string leanOp(const std::string& op) {
    if (op == "&") return "&&&";
    if (op == "|") return "|||";
    if (op == "^") return "^^^";
    if (op == "<<") return "<<<";
    if (op == ">>") return ">>>";
    return op;
}

Type cmpType(const Expr& e) { return unify(e.kids[0].type, e.kids[1].type, true).type; }

// Level of the Lean text of `e` when used as a value (a `bool`-valued test
// included). 100 = an atom that needs no parentheses anywhere.
int level(const Expr& e) {
    switch (e.kind) {
    case Expr::Kind::Binary: return leanLevel(e.text);
    case Expr::Kind::Cmp:    return cmpIsBool(cmpType(e), e.text) ? 50 : 90;  // `a == b` / `decide (…)`
    case Expr::Kind::And:    return 35;
    case Expr::Kind::Or:     return 30;
    case Expr::Kind::Not:
    case Expr::Kind::Cast:
    case Expr::Kind::Call:   return 90;  // applications
    default:                 return 100;
    }
}

void emitExpr(const Expr& e, std::string& out);

// `e` as an argument or as the receiver of `.toNat`: parenthesised unless atomic.
void emitAtom(const Expr& e, std::string& out) {
    if (level(e) == 100) {
        emitExpr(e, out);
        return;
    }
    out += '(';
    emitExpr(e, out);
    out += ')';
}

void emitChild(const Expr& e, int parentLevel, bool right, std::string& out) {
    const int l = level(e);
    const bool paren = right ? l <= parentLevel : l < parentLevel;  // `<=` keeps associativity visible
    if (paren) out += '(';
    emitExpr(e, out);
    if (paren) out += ')';
}

void emitExpr(const Expr& e, std::string& out) {
    switch (e.kind) {
    case Expr::Kind::IntLit:
    case Expr::Kind::FloatLit:
    case Expr::Kind::CharLit:
    case Expr::Kind::StringLit:
    case Expr::Kind::BoolLit:
    case Expr::Kind::Ident:
        out += e.text;
        return;
    case Expr::Kind::Index:  // `a[i]!`, and on a string `s.toList[i]!` (main-spec §4.3, §4.4)
        emitAtom(e.kids[0], out);
        if (e.kids[0].type.kind == Type::Kind::String) out += ".toList";
        out += '[';
        emitExpr(e.kids[1], out);
        out += "]!";
        return;
    case Expr::Kind::ArrayLit:
        return;  // printed by emitArrayInit
    case Expr::Kind::BvLit:
        out += e.text + "#" + std::to_string(e.type.width);
        return;
    case Expr::Kind::UnaryNeg:
        out += "(-";  // always parenthesised: simple and unambiguous in every position
        emitAtom(e.kids[0], out);
        out += ")";
        return;
    case Expr::Kind::BitNot:
        out += "(~~~";
        emitAtom(e.kids[0], out);
        out += ")";
        return;
    case Expr::Kind::Cast: {
        std::string x;
        emitAtom(e.kids[0], x);
        out += convert(e.kids[0].type, e.type, x, nullptr, nullptr);  // check.cpp proved it exists
        return;
    }
    case Expr::Kind::Call:
        out += e.text;
        for (const Expr& arg : e.kids) {
            out += ' ';
            emitAtom(arg, out);
        }
        return;
    case Expr::Kind::Binary: {
        const int me = leanLevel(e.text);
        emitChild(e.kids[0], me, false, out);
        out += ' ' + leanOp(e.text) + ' ';
        emitChild(e.kids[1], me, true, out);
        return;
    }
    // ---- bool-valued tests (check.cpp only lets these through where a `bool` is wanted) ----
    case Expr::Kind::Cmp: {
        const Type t = cmpType(e);
        const std::string op = cmpSpelling(t, e.text);
        if (cmpIsBool(t, e.text)) {  // Float ==, != are already `Bool`
            emitChild(e.kids[0], 51, false, out);
            out += ' ' + op + ' ';
            emitChild(e.kids[1], 51, false, out);
            return;
        }
        out += "decide (";
        emitChild(e.kids[0], 51, false, out);
        out += ' ' + op + ' ';
        emitChild(e.kids[1], 51, false, out);
        out += ')';
        return;
    }
    case Expr::Kind::And:
    case Expr::Kind::Or: {
        const int me = level(e);
        emitChild(e.kids[0], me, false, out);
        out += e.kind == Expr::Kind::And ? " && " : " || ";
        emitChild(e.kids[1], me, true, out);
        return;
    }
    case Expr::Kind::Not:
        out += "!";
        emitAtom(e.kids[0], out);
        return;
    }
}

// ---- tests ----------------------------------------------------------------

// Looseness of a test: Or < And < everything else (comparisons, `x ≠ 0`, ¬(…)).
int condPrec(const Expr& e) {
    if (e.kind == Expr::Kind::Or) return 1;
    if (e.kind == Expr::Kind::And) return 2;
    return 3;
}

void emitCond(const Expr& e, std::string& out) {
    switch (e.kind) {
    case Expr::Kind::Cmp: {
        const Type t = cmpType(e);
        emitChild(e.kids[0], 51, false, out);
        out += ' ' + cmpSpelling(t, e.text) + ' ';
        emitChild(e.kids[1], 51, false, out);
        return;
    }
    case Expr::Kind::And:
    case Expr::Kind::Or: {
        const char* op = e.kind == Expr::Kind::And ? " ∧ " : " ∨ ";
        for (int k = 0; k < 2; ++k) {
            if (k) out += op;
            bool paren = condPrec(e.kids[k]) < condPrec(e);
            if (paren) out += '(';
            emitCond(e.kids[k], out);
            if (paren) out += ')';
        }
        return;
    }
    case Expr::Kind::Not:
        out += "¬(";
        emitCond(e.kids[0], out);
        out += ')';
        return;
    default: {  // a value used as a test: C's "non-zero is true" (types.cpp decides the spelling)
        std::string v;
        emitExpr(e, v);
        out += truthTest(e.type, v);
    }
    }
}

// ---- comments -------------------------------------------------------------

// `//` → `--` lines; `/* */` → `/- -/`. Lean block comments nest, so a C
// comment that itself contains `/-` or `-/` would corrupt the output; those
// fall back to `--` lines.
void emitComments(const std::vector<Comment>& comments, const std::string& ind, std::string& out) {
    for (const Comment& c : comments) {
        std::vector<std::string> lines;  // a /* */ comment may span lines
        size_t start = 0;
        while (start <= c.text.size()) {
            size_t nl = c.text.find('\n', start);
            std::string line = trimBlank(
                c.text.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
            if (!line.empty()) lines.push_back(line);
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        if (lines.empty()) continue;
        bool safe = c.text.find("/-") == std::string::npos && c.text.find("-/") == std::string::npos;
        if (c.block && safe) {
            out += ind + "/- " + lines[0];
            for (size_t k = 1; k < lines.size(); ++k) out += "\n" + ind + "   " + lines[k];
            out += " -/\n";
        } else {
            for (const std::string& line : lines) out += ind + "-- " + line + "\n";
        }
    }
}

// ---- statements -----------------------------------------------------------

// `wret` names the array parameter a `void` writer function returns (OD11 c);
// empty for ordinary functions, which take none of the writer paths. A block
// emitted with `value` = true must end in that array: a path that falls off
// the end gets `return wret`, and an `if`/`match` in last position (`last`,
// after flattening bare blocks) is emitted total, so it yields the value.

void emitStmts(const std::vector<Stmt>& list, int level, const std::string& wret, bool value,
               std::string& out, const std::vector<Comment>* trailing = nullptr);
void emitList(const std::vector<Stmt>& list, int level, const std::string& wret, const Stmt* last,
              std::string& out);

// The statement that really ends a `do` stream: bare `{ }` blocks are
// flattened into their parent, so the last statement may sit inside them.
const Stmt* lastReal(const std::vector<Stmt>& list) {
    if (list.empty()) return nullptr;
    const Stmt* s = &list.back();
    while (s->kind == Stmt::Kind::Block) {
        if (s->body.empty()) return nullptr;  // the stream ends in nothing
        s = &s->body.back();
    }
    return s;
}

void emitIf(const Stmt& s, int level, const std::string& wret, bool yield, std::string& out) {
    const Stmt* cur = &s;
    out += pad(level) + "if ";
    emitCond(cur->expr, out);
    out += " then\n";
    emitStmts(cur->body, level + 1, wret, yield, out, &cur->bodyTrailing);
    while (!cur->elseBody.empty()) {
        const std::vector<Stmt>& e = cur->elseBody;
        if (e.size() == 1 && e[0].kind == Stmt::Kind::If && e[0].comments.empty()) {
            cur = &e[0];  // `else if` chain stays flat
            out += pad(level) + "else if ";
            emitCond(cur->expr, out);
            out += " then\n";
            emitStmts(cur->body, level + 1, wret, yield, out, &cur->bodyTrailing);
        } else {
            out += pad(level) + "else\n";
            emitStmts(e, level + 1, wret, yield, out, &cur->elseTrailing);
            return;
        }
    }
    if (yield)  // a value-yielding `if` must be total: a missing else returns the array as it is
        out += pad(level) + "else\n" + pad(level + 1) + "return " + wret + "\n";
}

void emitSwitch(const Stmt& s, int level, const std::string& wret, bool yield, std::string& out) {
    out += pad(level) + "match ";
    emitExpr(s.expr, out);
    out += " with\n";
    const Case* dflt = nullptr;
    auto arm = [&](const Case& c, const std::string& pattern) {
        out += pad(level) + "| " + pattern + " =>\n";
        emitStmts(c.body, level + 1, wret, yield, out, &c.bodyTrailing);
    };
    for (const Case& c : s.cases) {
        if (c.isDefault) {  // `default` wins the whole group; it goes last (labels are unique)
            dflt = &c;
            continue;
        }
        std::string pattern;
        for (const Expr& lab : c.labels) {
            if (!pattern.empty()) pattern += " | ";
            emitExpr(lab, pattern);
        }
        arm(c, pattern);
    }
    if (dflt) {
        arm(*dflt, "_");
    } else {  // C: no default → nothing happens (a writer: return the array as it is)
        out += pad(level) + "| _ =>\n" + pad(level + 1) +
               (yield ? "return " + wret : "pure ()") + "\n";
    }
}

// ---- arrays ---------------------------------------------------------------

// `x` as a function argument: parenthesised when it has a space.
std::string arg(const std::string& x) { return x.find(' ') == std::string::npos ? x : "(" + x + ")"; }

// "All zero" of `t`, an array level deeper when `t` is an array (sizes from `dims`).
std::string zeroOf(Type t, const std::vector<long long>& dims, size_t depth) {
    if (t.kind != Type::Kind::Array) return zeroValue(t);
    return "Array.replicate " + std::to_string(dims[depth]) + " " + arg(zeroOf(t.elem(), dims, depth + 1));
}

std::string arrayLit(const Expr& lit, Type arr, const std::vector<long long>& dims, size_t depth) {
    const long long n = dims[depth];
    const long long k = (long long)lit.kids.size();
    const Type elem = arr.elem();
    std::vector<std::string> parts;
    for (const Expr& kid : lit.kids) {
        std::string t;
        if (elem.kind == Type::Kind::Array) t = arrayLit(kid, elem, dims, depth + 1);
        else emitExpr(kid, t);
        parts.push_back(t);
    }
    const std::string zero = zeroOf(elem, dims, depth + 1);
    const std::string fill = "Array.replicate " + std::to_string(n) + " " + arg(zero);
    if (k == 0 || (k == 1 && parts[0] == zero && n > 1)) return fill;  // `{0}` → replicate (§4.4)
    std::string list = "#[";
    for (size_t i = 0; i < parts.size(); ++i) list += (i ? ", " : "") + parts[i];
    list += "]";
    if (k == n) return list;
    // C zero-fills the rest of a short list.
    return list + " ++ Array.replicate " + std::to_string(n - k) + " " + arg(zero);
}

// The initial value of an array local: its list, or default values (§4.4).
std::string arrayInit(const Stmt& s) {
    if (!s.noInit) return arrayLit(s.expr, s.type, s.dims, 0);
    std::string out = "default";
    for (size_t d = s.dims.size(); d-- > 0;) out = "Array.replicate " + std::to_string(s.dims[d]) + " " + arg(out);
    return out;
}

// `a.set! i v`, or for a nested element `a.set! i (a[i]!.set! j v)`.
std::string setElement(const std::string& base, const std::vector<Expr>& idx, size_t k, const std::string& v) {
    std::string at;
    emitAtom(idx[k], at);
    if (k + 1 == idx.size()) return base + ".set! " + at + " " + v;
    std::string raw;
    emitExpr(idx[k], raw);
    return base + ".set! " + at + " (" + setElement(base + "[" + raw + "]!", idx, k + 1, v) + ")";
}

// The statements themselves, no `do`-sequence housekeeping.
void emitList(const std::vector<Stmt>& list, int level, const std::string& wret, const Stmt* last,
              std::string& out) {
    for (const Stmt& s : list) {
        emitComments(s.comments, pad(level), out);
        switch (s.kind) {
        case Stmt::Kind::Decl:
            out += pad(level) + (s.mut ? "let mut " : "let ") + s.name + " : " + leanName(s.type) + " := ";
            if (s.type.kind == Type::Kind::Array) out += arrayInit(s);
            else emitExpr(s.expr, out);
            out += "\n";
            break;
        case Stmt::Kind::Assign:
            out += pad(level) + s.name + " := ";
            if (s.indices.empty()) {
                emitExpr(s.expr, out);
            } else {  // `a[i][j] = v;` → `a.set! i (a[i]!.set! j v)` (§4.4)
                std::string v;
                emitAtom(s.expr, v);
                out += setElement(s.name, s.indices, 0, v);
            }
            out += "\n";
            break;
        case Stmt::Kind::CallStmt:
            if (!s.writeBack.empty()) {  // a writer call: the modified array comes back (OD11 c)
                out += pad(level) + s.writeBack + " := ";
            } else {  // a plain `void` call: pure, so it can only be run for show
                out += pad(level) + "let _ := ";
            }
            emitExpr(s.expr, out);
            out += "\n";
            break;
        case Stmt::Kind::Return:
            out += pad(level) + "return ";
            if (s.noValue) out += wret.empty() ? "()" : wret;  // a writer returns its array
            else emitExpr(s.expr, out);
            out += "\n";
            break;
        case Stmt::Kind::If:
            emitIf(s, level, wret, !wret.empty() && &s == last, out);
            break;
        case Stmt::Kind::Switch:
            emitSwitch(s, level, wret, !wret.empty() && &s == last, out);
            break;
        case Stmt::Kind::Block:
            // Lean's nested `do` is not a scope (its `let`s leak), so a bare C block is
            // flattened; check.cpp refused the blocks where that would change meaning.
            // The flattened stream keeps the same last statement.
            emitList(s.body, level, wret, last, out);
            emitComments(s.bodyTrailing, pad(level), out);
            break;
        case Stmt::Kind::Break:
            break;  // stripped by the parser
        }
    }
}

void emitStmts(const std::vector<Stmt>& list, int level, const std::string& wret, bool value,
               std::string& out, const std::vector<Comment>* trailing) {
    const Stmt* last = lastReal(list);
    emitList(list, level, wret, last, out);
    if (trailing) emitComments(*trailing, pad(level), out);
    if (value) {
        // A writer's block: a path that falls off the end returns the array as it is.
        // A `return` already ends it; a total `if`/`match` in last position yields it.
        if (!(last && (last->kind == Stmt::Kind::Return || last->kind == Stmt::Kind::If ||
                       last->kind == Stmt::Kind::Switch)))
            out += pad(level) + "return " + wret + "\n";
        return;
    }
    // A Lean `do` sequence cannot end in `let`, and cannot be empty.
    if (list.empty() || list.back().kind == Stmt::Kind::Decl || list.back().kind == Stmt::Kind::CallStmt)
        out += pad(level) + "pure ()\n";
}

// ---- functions ------------------------------------------------------------

void emitFunction(const Function& f, const std::string& fileName, std::string& out) {
    out += "-- c: " + fileName + ":" + std::to_string(f.defLine) + "\n";
    emitComments(f.comments, "", out);
    const bool writer = f.writerParam >= 0;  // OD11 (c): the written array parameter comes back
    const std::string wret = writer ? f.params[(size_t)f.writerParam].second : "";
    // A writer's one-statement body can only be `return;`, which needs the `do`
    // block's `return a` — so writers never take the single-expression shortcut.
    bool oneReturn = !writer && f.body.size() == 1 && f.body[0].kind == Stmt::Kind::Return;
    if (oneReturn) emitComments(f.body[0].comments, "", out);  // no `do` block to hold them
    if (f.recursive && !f.isPartial)
        out += "-- note: recursive. Lean must prove it terminates; if it cannot, put "
               "[[lean::partial]] on the C function.\n";

    out += f.isPartial ? "partial def " : "def ";
    out += f.name;
    if (!f.params.empty()) {
        out += " (";
        bool first = true;
        size_t i = 0;
        while (i < f.params.size()) {  // merge runs of same-typed parameters (§7.1)
            const Type ty = f.params[i].first;
            std::string names;
            size_t j = i;
            for (; j < f.params.size() && f.params[j].first == ty; ++j) {
                if (!names.empty()) names += ' ';
                names += f.params[j].second;
            }
            if (!first) out += ") (";
            first = false;
            out += names;
            out += " : " + leanName(ty);
            i = j;
        }
        out += ")";
    }
    // A writer returns the array it modified instead of `Unit` (OD11 c).
    out += " : " + leanName(writer ? f.params[(size_t)f.writerParam].first : f.retType) + " := ";
    if (oneReturn) {
        if (f.body[0].noValue) out += "()";
        else emitExpr(f.body[0].expr, out);
        out += "\n";
        emitComments(f.bodyTrailing, "", out);  // no `do` block to hold them
        return;
    }
    out += "Id.run do\n";
    for (const std::string& p : f.mutParams) {  // a parameter that is assigned (§3.3)
        Type ty;
        for (const auto& param : f.params)
            if (param.second == p) ty = param.first;
        out += pad(1) + "let mut " + p + " : " + leanName(ty) + " := " + p + "\n";
    }
    emitStmts(f.body, 1, wret, writer, out, &f.bodyTrailing);
}

}  // namespace

std::string emit(const TranslationUnit& unit) {
    std::string out;
    out += "-- generated by clean from " + unit.fileName + " — do not edit\n";
    out += "set_option autoImplicit false\n";

    size_t i = 0;
    while (i < unit.functions.size()) {
        const Function& f = unit.functions[i];
        out += "\n";
        if (f.mutualGroup < 0) {
            emitFunction(f, unit.fileName, out);
            ++i;
            continue;
        }
        out += "mutual\n";  // the group's members are adjacent (toposort.cpp)
        bool first = true;
        for (; i < unit.functions.size() && unit.functions[i].mutualGroup == f.mutualGroup; ++i) {
            if (!first) out += "\n";
            first = false;
            emitFunction(unit.functions[i], unit.fileName, out);
        }
        out += "end\n";
    }
    if (!unit.trailingComments.empty()) {
        out += "\n";
        emitComments(unit.trailingComments, "", out);
    }
    return out;
}
