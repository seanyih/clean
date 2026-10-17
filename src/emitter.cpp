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
//     share a `mutual … end` block (set up by toposort.cpp).

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

// Binding power; higher binds tighter. Only used to place parentheses.
int prec(const Expr& e) {
    switch (e.kind) {
    case Expr::Kind::Binary:
        return (e.text == "+" || e.text == "-") ? 1 : 2;  // * / %
    case Expr::Kind::UnaryNeg:
        return 3;
    default:
        return 4;  // literals, names, calls (application binds tightest in Lean)
    }
}

bool atomic(const Expr& e) {
    return e.kind == Expr::Kind::IntLit || e.kind == Expr::Kind::Ident;
}

void emitExpr(const Expr& e, std::string& out) {
    switch (e.kind) {
    case Expr::Kind::IntLit:
    case Expr::Kind::Ident:
        out += e.text;
        return;
    case Expr::Kind::UnaryNeg:
        out += "(-";  // always parenthesised: simple and unambiguous in every position
        emitExpr(e.kids[0], out);
        out += ")";
        return;
    case Expr::Kind::Call:
        out += e.text;
        for (const Expr& arg : e.kids) {
            out += ' ';
            if (!atomic(arg)) {  // an argument that is not an atom needs parentheses
                out += '(';
                emitExpr(arg, out);
                out += ')';
            } else {
                emitExpr(arg, out);
            }
        }
        return;
    case Expr::Kind::Binary: {
        const Expr& l = e.kids[0];
        const Expr& r = e.kids[1];
        if (prec(l) < prec(e)) {
            out += '(';
            emitExpr(l, out);
            out += ')';
        } else {
            emitExpr(l, out);
        }
        out += ' ';
        out += e.text;
        out += ' ';
        if (prec(r) <= prec(e)) {  // `<=` keeps the tree's associativity visible
            out += '(';
            emitExpr(r, out);
            out += ')';
        } else {
            emitExpr(r, out);
        }
        return;
    }
    default:
        return;  // tests never reach here: check.cpp keeps them out of value positions
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
        emitExpr(e.kids[0], out);
        out += ' ';
        out += e.text == "==" ? "=" : e.text == "!=" ? "≠" : e.text;
        out += ' ';
        emitExpr(e.kids[1], out);
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
    default:  // a number used as a test: C's "non-zero is true"
        emitExpr(e, out);
        out += truthSuffix(e.type);
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

void emitStmts(const std::vector<Stmt>& list, int level, std::string& out);
void emitList(const std::vector<Stmt>& list, int level, std::string& out);

void emitIf(const Stmt& s, int level, std::string& out) {
    const Stmt* cur = &s;
    out += pad(level) + "if ";
    emitCond(cur->expr, out);
    out += " then\n";
    emitStmts(cur->body, level + 1, out);
    while (!cur->elseBody.empty()) {
        const std::vector<Stmt>& e = cur->elseBody;
        if (e.size() == 1 && e[0].kind == Stmt::Kind::If && e[0].comments.empty()) {
            cur = &e[0];  // `else if` chain stays flat
            out += pad(level) + "else if ";
            emitCond(cur->expr, out);
            out += " then\n";
            emitStmts(cur->body, level + 1, out);
        } else {
            out += pad(level) + "else\n";
            emitStmts(e, level + 1, out);
            break;
        }
    }
}

void emitSwitch(const Stmt& s, int level, std::string& out) {
    out += pad(level) + "match ";
    emitExpr(s.expr, out);
    out += " with\n";
    const Case* dflt = nullptr;
    auto arm = [&](const Case& c, const std::string& pattern) {
        out += pad(level) + "| " + pattern + " =>\n";
        emitStmts(c.body, level + 1, out);
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
    } else {  // C: no default → nothing happens
        out += pad(level) + "| _ =>\n" + pad(level + 1) + "pure ()\n";
    }
}

// The statements themselves, no `do`-sequence housekeeping.
void emitList(const std::vector<Stmt>& list, int level, std::string& out) {
    for (const Stmt& s : list) {
        emitComments(s.comments, pad(level), out);
        switch (s.kind) {
        case Stmt::Kind::Decl:
            out += pad(level) + (s.mut ? "let mut " : "let ") + s.name + " : " + leanName(s.type) + " := ";
            emitExpr(s.expr, out);
            out += "\n";
            break;
        case Stmt::Kind::Assign:
            out += pad(level) + s.name + " := ";
            emitExpr(s.expr, out);
            out += "\n";
            break;
        case Stmt::Kind::Return:
            out += pad(level) + "return ";
            emitExpr(s.expr, out);
            out += "\n";
            break;
        case Stmt::Kind::If:
            emitIf(s, level, out);
            break;
        case Stmt::Kind::Switch:
            emitSwitch(s, level, out);
            break;
        case Stmt::Kind::Block:
            // Lean's nested `do` is not a scope (its `let`s leak), so a bare C block is
            // flattened; check.cpp refused the blocks where that would change meaning.
            emitList(s.body, level, out);
            break;
        case Stmt::Kind::Break:
            break;  // stripped by the parser
        }
    }
}

void emitStmts(const std::vector<Stmt>& list, int level, std::string& out) {
    emitList(list, level, out);
    // A Lean `do` sequence cannot end in `let`, and cannot be empty.
    if (list.empty() || list.back().kind == Stmt::Kind::Decl) out += pad(level) + "pure ()\n";
}

// ---- functions ------------------------------------------------------------

void emitFunction(const Function& f, const std::string& fileName, std::string& out) {
    out += "-- c: " + fileName + ":" + std::to_string(f.defLine) + "\n";
    emitComments(f.comments, "", out);
    bool oneReturn = f.body.size() == 1 && f.body[0].kind == Stmt::Kind::Return;
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
    out += " : " + leanName(f.retType) + " := ";
    if (oneReturn) {
        emitExpr(f.body[0].expr, out);
        out += "\n";
        return;
    }
    out += "Id.run do\n";
    for (const std::string& p : f.mutParams) {  // a parameter that is assigned (§3.3)
        Type ty;
        for (const auto& param : f.params)
            if (param.second == p) ty = param.first;
        out += pad(1) + "let mut " + p + " : " + leanName(ty) + " := " + p + "\n";
    }
    emitStmts(f.body, 1, out);
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
    return out;
}
