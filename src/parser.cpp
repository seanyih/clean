// parser.cpp — implementation of parser.h.
//
// Recursive descent over the slice-0 grammar. The second half is the
// validation pass that runs after parsing: every identifier must be a
// parameter or a defined function, and arities must match. All of this is
// checked here so the emitted Lean has no name errors left to find. Order
// is not checked: toposort.cpp reorders the definitions afterwards.

#include "parser.h"

#include <map>
#include <utility>

namespace {

const char* kSliceWhy =
    "clean is at slice 0: only `int` functions with a single `return <expr>;` "
    "body are translated (see spec/impl-spec.md).";

// Names Lean reserves. Not exhaustive; it exists so a collision fails here,
// loudly, instead of inside Lean (main-spec D8).
const std::set<std::string> kLeanKeywords = {
    "def", "theorem", "example", "abbrev", "structure", "inductive", "axiom",
    "let", "mut", "fun", "if", "then", "else", "match", "with", "do",
    "return", "import", "open", "namespace", "variable", "partial", "private",
    "while", "for", "in", "break", "continue", "sorry", "by", "from", "have",
    "show", "Type", "Prop", "Unit", "Nat", "Int", "Bool", "String", "Char",
    "Float", "BitVec", "Array", "List", "Option", "true", "false",
};

// C type / storage-class spellings this build does not translate yet. Better
// a pointed refusal than a bare "expected a declaration".
const std::set<std::string> kUntranslated = {
    "void", "char", "signed", "unsigned", "short", "long", "float", "double",
    "bool", "_Bool", "size_t", "struct", "union", "enum", "typedef", "const",
    "static", "extern", "inline", "restrict", "volatile", "register",
    "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t",
    "uint32_t", "uint64_t", "bv", "_BitVec",
};

[[noreturn]] void fail(const std::string& file, Loc loc, const std::string& what,
                       const std::string& why, const std::string& fix) {
    throw DiagError("E000", file, loc, what, why, fix);
}

std::string describe(const Token& t) {
    switch (t.kind) {
    case Token::Kind::End:     return "end of file";
    case Token::Kind::Ident:   return "`" + t.text + "`";
    case Token::Kind::IntLit:  return "literal `" + t.text + "`";
    case Token::Kind::Comment:return "a comment";
    case Token::Kind::Punct:   return "`" + t.text + "`";
    case Token::Kind::Other:   return "`" + t.text + "`";
    }
    return "";
}

struct Parser {
    const std::vector<Token>& toks;
    const std::set<std::string>& macros;
    const std::string& file;
    size_t i = 0;

    const Token& peek() const { return toks[i]; }
    const Token& peek2() const { return toks[i + 1]; }  // safe unless peek() is End
    bool atEnd() const { return peek().kind == Token::Kind::End; }
    bool isPunct(const std::string& s) const {
        return peek().kind == Token::Kind::Punct && peek().text == s;
    }
    bool isIdent(const std::string& s) const {
        return peek().kind == Token::Kind::Ident && peek().text == s;
    }
    void next() {
        if (!atEnd()) ++i;
    }
    void skipComments() {
        while (peek().kind == Token::Kind::Comment) ++i;
    }

    [[noreturn]] void failHere(const std::string& what, const std::string& why,
                               const std::string& fix) {
        fail(file, peek().loc, what, why, fix);
    }

    void expectPunct(const std::string& p, const std::string& ctx) {
        if (!isPunct(p))
            failHere("expected `" + p + "` " + ctx + ", found " + describe(peek()),
                     kSliceWhy, "fix the C source; clean does not repair syntax.");
        ++i;
    }

    Token expectIdent(const std::string& ctx) {
        if (peek().kind != Token::Kind::Ident)
            failHere("expected " + ctx + ", found " + describe(peek()), kSliceWhy,
                     "fix the C source; clean does not repair syntax.");
        return toks[i++];
    }

    // D8: a name that Lean reserves is rejected; the tool never renames.
    void checkName(const Token& t) {
        if (kLeanKeywords.count(t.text))
            fail(file, t.loc, "`" + t.text + "` is reserved by Lean and cannot be used as a name",
                 "the translator preserves names verbatim (D8), and this one would break the output.",
                 "rename the identifier in the C source.");
    }

    // ---- expressions ---------------------------------------------------

    Expr parseExpr() { return parseBinary(1); }

    Expr parseBinary(int minPrec) {
        Expr lhs = parseUnary();
        while (true) {
            const Token& op = peek();
            if (op.kind != Token::Kind::Punct) break;
            int p = 0;
            if (op.text == "+" || op.text == "-") p = 1;
            else if (op.text == "*" || op.text == "/" || op.text == "%") p = 2;
            if (p < minPrec) break;
            ++i;
            Expr rhs = parseBinary(p + 1);
            Expr e;
            e.kind = Expr::Kind::Binary;
            e.text = op.text;
            e.loc = lhs.loc;
            e.kids.push_back(std::move(lhs));
            e.kids.push_back(std::move(rhs));
            lhs = std::move(e);
        }
        return lhs;
    }

    Expr parseUnary() {
        if (isPunct("-")) {
            Token op = toks[i++];
            Expr e;
            e.kind = Expr::Kind::UnaryNeg;
            e.loc = op.loc;
            e.kids.push_back(parseUnary());
            return e;
        }
        return parsePrimary();
    }

    Expr parsePrimary() {
        const Token& t = peek();
        if (t.kind == Token::Kind::IntLit) {
            ++i;
            Expr e;
            e.kind = Expr::Kind::IntLit;
            e.text = t.text;
            e.loc = t.loc;
            return e;
        }
        if (t.kind == Token::Kind::Ident) {
            if (macros.count(t.text))
                fail(file, t.loc,
                     "macro `" + t.text + "` is used, but macros are not expanded in this slice",
                     "a macro name is not a Lean name; expanding it would be guessing.",
                     "write the value directly, or wait for preprocessor support.");
            ++i;
            if (isPunct("(")) {  // function call
                ++i;
                Expr e;
                e.kind = Expr::Kind::Call;
                e.text = t.text;
                e.loc = t.loc;
                if (!isPunct(")")) {
                    e.kids.push_back(parseExpr());
                    while (isPunct(",")) {
                        ++i;
                        e.kids.push_back(parseExpr());
                    }
                }
                expectPunct(")", "to close the argument list of `" + t.text + "`");
                return e;
            }
            Expr e;
            e.kind = Expr::Kind::Ident;
            e.text = t.text;
            e.loc = t.loc;
            return e;
        }
        if (isPunct("(")) {
            ++i;
            Expr e = parseExpr();
            expectPunct(")", "to close the parenthesised expression");
            return e;
        }
        failHere("expected an expression, found " + describe(peek()), kSliceWhy,
                 "slice-0 expressions are literals, names, calls, and + - * / % with parentheses.");
    }

    // ---- declarations --------------------------------------------------

    std::vector<std::pair<std::string, std::string>> parseParams() {
        std::vector<std::pair<std::string, std::string>> params;
        if (isIdent("void") && peek2().kind == Token::Kind::Punct && peek2().text == ")") {
            ++i;  // `void`
            return params;  // the `)` stays for the caller
        }
        if (isPunct(")")) return params;  // `f()`
        while (true) {
            if (!isIdent("int"))
                failHere("expected an `int` parameter, found " + describe(peek()),
                         "slice-0 parameters are `int` only (→ Lean `Int`).",
                         "use `int`; other types arrive slice by slice (spec/impl-spec.md).");
            ++i;
            Token name = expectIdent("a parameter name");
            checkName(name);
            params.emplace_back("int", name.text);
            if (isPunct(",")) {
                ++i;
                continue;
            }
            break;
        }
        return params;
    }

    // The `int`, the name, the parameter list are already consumed by the caller.
    Function parseFunction(std::string name, int defLine,
                           std::vector<std::pair<std::string, std::string>> params,
                           std::vector<Comment> comments) {
        Function f;
        f.name = std::move(name);
        f.defLine = defLine;
        f.params = std::move(params);
        f.comments = std::move(comments);
        expectPunct("{", "to open the body of `" + f.name + "`");
        skipComments();  // comments inside bodies are dropped in this slice (impl-spec)
        if (!isIdent("return"))
            failHere("the body of `" + f.name + "` must be a single `return <expr>;` in this slice",
                     "statements (locals, `if`, loops, …) are translated from the next slice on.",
                     "write the body as one return expression for now.");
        ++i;
        f.body = parseExpr();
        expectPunct(";", "after the return expression");
        skipComments();
        expectPunct("}", "to close the body of `" + f.name + "`");
        return f;
    }
};

// ---- validation pass ----------------------------------------------------

// Every name the emitted Lean will mention must be known: parameters are
// values and functions are called with the right arity. (Definition order is
// fixed later by toposort.cpp, so a callee may sit above or below its caller.)
void checkExpr(const Expr& e, const std::string& file, const std::set<std::string>& params,
               const std::map<std::string, size_t>& sigs) {
    switch (e.kind) {
    case Expr::Kind::IntLit:
        return;
    case Expr::Kind::Ident:
        if (params.count(e.text)) return;
        if (sigs.count(e.text))
            fail(file, e.loc, "function `" + e.text + "` is used as a value",
                 "slice 0 has no function values (function pointers are ⛔ in the main spec).",
                 "call it: `" + e.text + "(…)`, or pass an argument.");
        fail(file, e.loc, "`" + e.text + "` is neither a parameter nor a defined function",
             "clean rejects unknown names instead of emitting Lean that fails later (D5).",
             "declare it, define it, or fix the typo.");
    case Expr::Kind::UnaryNeg:
        checkExpr(e.kids[0], file, params, sigs);
        return;
    case Expr::Kind::Binary:
        checkExpr(e.kids[0], file, params, sigs);
        checkExpr(e.kids[1], file, params, sigs);
        return;
    case Expr::Kind::Call: {
        if (params.count(e.text))
            fail(file, e.loc, "parameter `" + e.text + "` is called like a function",
                 "function values are not translated in this slice.",
                 "call a defined function, or drop the call.");
        auto it = sigs.find(e.text);
        if (it == sigs.end())
            fail(file, e.loc, "`" + e.text + "` is called but not defined in this file",
                 "slice 0 resolves calls inside the file; `extern` declarations come later.",
                 "define `" + e.text + "` in the same file.");
        if (it->second != e.kids.size())
            fail(file, e.loc, "`" + e.text + "` takes " + std::to_string(it->second) +
                     " argument(s) but is called with " + std::to_string(e.kids.size()),
                 "the arity is checked here so that Lean does not have to report it.",
                 "match the parameter list.");
        for (const Expr& a : e.kids)
            checkExpr(a, file, params, sigs);
        return;
    }
    }
}

}  // namespace

TranslationUnit parse(const std::vector<Token>& tokens,
                      const std::set<std::string>& definedNames,
                      const std::string& fileName) {
    Parser p{tokens, definedNames, fileName, 0};
    TranslationUnit unit;
    unit.fileName = fileName;

    std::vector<Comment> pending;  // comments waiting for the next definition
    while (true) {
        while (p.peek().kind == Token::Kind::Comment) {
            pending.push_back({p.peek().text, p.peek().block});
            ++p.i;
        }
        if (p.atEnd()) break;
        if (p.isPunct(";")) {  // stray semicolon at file scope (C23 allows it)
            ++p.i;
            continue;
        }
        if (p.peek().kind != Token::Kind::Ident)
            p.failHere("expected a declaration, found " + describe(p.peek()), kSliceWhy,
                       "slice-0 files contain `int` functions; see spec/impl-spec.md.");
        if (p.peek().text != "int") {
            std::string ty = p.peek().text;
            std::string why = kUntranslated.count(ty)
                ? "`" + ty + "` exists in the main spec but is not translated in this slice."
                : "slice 0 translates only `int` functions.";
            p.failHere("type `" + ty + "` is not translated in this slice", why,
                       "use `int` for now; the type table grows slice by slice (spec/impl-spec.md).");
        }
        int defLine = p.peek().loc.line;
        ++p.i;  // `int`
        Token name = p.expectIdent("a function name");
        p.checkName(name);
        p.expectPunct("(", "to open the parameter list of `" + name.text + "`");
        auto params = p.parseParams();
        p.expectPunct(")", "to close the parameter list of `" + name.text + "`");
        if (p.isPunct(";")) {  // prototype: dropped (§7.1)
            ++p.i;
            continue;
        }
        unit.functions.push_back(
            p.parseFunction(name.text, defLine, std::move(params), std::move(pending)));
        pending.clear();
    }

    // Name and arity checks over the parsed tree.
    std::map<std::string, size_t> sigs;  // name → arity
    for (const Function& f : unit.functions) {
        if (sigs.count(f.name))
            fail(fileName, Loc{f.defLine, 1}, "function `" + f.name + "` is defined twice",
                 "Lean allows only one definition per name.", "rename one of them.");
        sigs[f.name] = f.params.size();
    }
    for (const Function& f : unit.functions) {
        std::set<std::string> params;
        for (const auto& pr : f.params) params.insert(pr.second);
        checkExpr(f.body, fileName, params, sigs);
    }
    return unit;
}
