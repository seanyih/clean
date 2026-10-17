// parser.cpp — implementation of parser.h.
//
// Recursive descent over the grammar in parser.h. It only builds the tree and
// refuses what it cannot build; whether names exist, arities match and every
// path returns is check.cpp's job. Two small desugarings happen here because
// they are purely syntactic: `x += e` → `x = x + e`, and `x++;` → `x = x + 1`.

#include "parser.h"

#include <utility>

namespace {

const char* kSliceWhy =
    "clean translates `int` and `unsigned` functions built from locals, assignments, `if`, "
    "`switch` and `return`; other statements and types arrive slice by slice "
    "(see spec/impl-spec.md).";

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
    "void", "char", "signed", "short", "long", "float", "double",
    "bool", "_Bool", "size_t", "struct", "union", "enum", "typedef", "const",
    "static", "extern", "inline", "restrict", "volatile", "register",
    "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t",
    "uint32_t", "uint64_t", "bv", "_BitVec",
};

// Statement keywords that come in a later slice.
const std::set<std::string> kLaterStatements = {"while", "for", "do", "goto", "continue"};

[[noreturn]] void failCode(const char* code, const std::string& file, Loc loc,
                           const std::string& what, const std::string& why,
                           const std::string& fix) {
    throw DiagError(code, file, loc, what, why, fix);
}

[[noreturn]] void fail(const std::string& file, Loc loc, const std::string& what,
                       const std::string& why, const std::string& fix) {
    failCode("E000", file, loc, what, why, fix);
}

std::string describe(const Token& t) {
    switch (t.kind) {
    case Token::Kind::End:     return "end of file";
    case Token::Kind::Ident:   return "`" + t.text + "`";
    case Token::Kind::IntLit:  return "literal `" + t.text + "`";
    case Token::Kind::Comment: return "a comment";
    case Token::Kind::Punct:   return "`" + t.text + "`";
    case Token::Kind::Other:   return "`" + t.text + "`";
    }
    return "";
}

// Binary operator precedence, loosest first (the C table).
int binaryPrec(const std::string& op) {
    if (op == "||") return 1;
    if (op == "&&") return 2;
    if (op == "==" || op == "!=") return 3;
    if (op == "<" || op == "<=" || op == ">" || op == ">=") return 4;
    if (op == "+" || op == "-") return 5;
    if (op == "*" || op == "/" || op == "%") return 6;
    return 0;
}

Expr::Kind binaryKind(const std::string& op) {
    if (op == "||") return Expr::Kind::Or;
    if (op == "&&") return Expr::Kind::And;
    if (binaryPrec(op) == 3 || binaryPrec(op) == 4) return Expr::Kind::Cmp;
    return Expr::Kind::Binary;
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
    void skipComments() {
        while (peek().kind == Token::Kind::Comment) ++i;
    }
    std::vector<Comment> takeComments() {
        std::vector<Comment> cs;
        while (peek().kind == Token::Kind::Comment) {
            cs.push_back({peek().text, peek().block});
            ++i;
        }
        return cs;
    }

    [[noreturn]] void failHere(const std::string& what, const std::string& why,
                               const std::string& fix) {
        fail(file, peek().loc, what, why, fix);
    }

    void expectPunct(const std::string& p, const std::string& ctx) {
        if (!isPunct(p))
            failHere("expected `" + p + "` " + ctx + ", found " + describe(peek()), kSliceWhy,
                     "fix the C source; clean does not repair syntax.");
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

    // The C scalar types this build translates (main-spec §4.1, the rows built
    // so far): `int` → Lean `Int`, `unsigned` (or `unsigned int`) → `Nat`.
    // Returns the spelling the AST stores: "int" / "unsigned".
    std::string parseCType() {
        const Token& t = peek();
        if (t.kind != Token::Kind::Ident)
            failHere("expected a type, found " + describe(peek()), kSliceWhy,
                     "write `int` or `unsigned`; see spec/impl-spec.md.");
        if (t.text == "int") {
            ++i;
            return "int";
        }
        if (t.text == "unsigned") {
            ++i;
            if (isIdent("int")) ++i;  // `unsigned int` — the same type
            if (peek().kind == Token::Kind::Ident && kUntranslated.count(peek().text))
                failHere("type `unsigned " + peek().text + "` is not translated in this slice",
                         "only plain `unsigned` is; the main spec maps the other unsigned "
                         "spellings to `Nat` too, but they arrive with the rest of the table.",
                         "write plain `unsigned` (→ `Nat`) or `int` for now.");
            return "unsigned";
        }
        if (kUntranslated.count(t.text))
            failHere("type `" + t.text + "` is not translated in this slice",
                     "`" + t.text + "` exists in the main spec but is not translated in this slice.",
                     "use `int` or `unsigned` for now; the type table grows slice by slice.");
        failHere("expected a type, found " + describe(peek()),
                 "only `int` (→ Lean `Int`) and `unsigned` (→ `Nat`) are translated in this slice.",
                 "write `int` or `unsigned`; the type table grows slice by slice.");
    }

    // A name in declaration position (`int int f()`, `unsigned unsigned x = 1;`).
    // Type words are rejected or the emitter would print a C keyword where the
    // output needs an identifier.
    Token expectName(const std::string& ctx) {
        if (peek().kind == Token::Kind::Ident &&
            (peek().text == "int" || peek().text == "unsigned" ||
             kUntranslated.count(peek().text)))
            failHere("expected " + ctx + ", found `" + peek().text + "`",
                     "`" + peek().text + "` names a type in C, not a variable or function.",
                     "name it something else.");
        return expectIdent(ctx);
    }

    // ---- expressions ---------------------------------------------------

    Expr parseExpr() {
        Expr e = parseBinary(1);
        if (isPunct("="))  // `if (x = 1)`, `return x = 2;`, `f(a = b)`
            failCode("E103", file, peek().loc, "assignment used as an expression",
                     "Lean has no assignment expressions; assignments are statements.",
                     "split it into a separate statement.");
        return e;
    }

    Expr parseBinary(int minPrec) {
        Expr lhs = parseUnary();
        while (true) {
            const Token& op = peek();
            if (op.kind != Token::Kind::Punct) break;
            int p = binaryPrec(op.text);
            if (p == 0 || p < minPrec) break;
            ++i;
            Expr rhs = parseBinary(p + 1);
            Expr e;
            e.kind = binaryKind(op.text);
            e.text = op.text;
            e.loc = lhs.loc;
            e.kids.push_back(std::move(lhs));
            e.kids.push_back(std::move(rhs));
            lhs = std::move(e);
        }
        return lhs;
    }

    [[noreturn]] void failIncDec() {
        failCode("E110", file, peek().loc, "`" + peek().text + "` inside an expression",
                 "an increment has a side effect, and Lean expressions have none.",
                 "write it as a separate statement (`x++;` or `x += 1;`).");
    }

    Expr parseUnary() {
        if (isPunct("++") || isPunct("--")) failIncDec();
        if (isPunct("-") || isPunct("!")) {
            Token op = toks[i++];
            Expr e;
            e.kind = op.text == "-" ? Expr::Kind::UnaryNeg : Expr::Kind::Not;
            e.loc = op.loc;
            e.kids.push_back(parseUnary());
            return e;
        }
        Expr e = parsePrimary();
        if (isPunct("++") || isPunct("--")) failIncDec();
        return e;
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
                 "expressions are literals, names, calls, arithmetic, and (in conditions) "
                 "comparisons with && || !.");
    }

    // ---- statements ----------------------------------------------------

    // `( expr )` after `if` / `switch`.
    Expr parseParenExpr(const std::string& what) {
        expectPunct("(", "after `" + what + "`");
        Expr e = parseExpr();
        expectPunct(")", "to close the `" + what + "` condition");
        return e;
    }

    Stmt makeAssign(const Token& name, Expr rhs) {
        Stmt s;
        s.kind = Stmt::Kind::Assign;
        s.name = name.text;
        s.expr = std::move(rhs);
        s.loc = name.loc;
        return s;
    }

    // `x = e;`  `x += e;`  `x++;`
    Stmt parseAssignment() {
        Token name = toks[i++];
        auto var = [&]() {
            Expr v;
            v.kind = Expr::Kind::Ident;
            v.text = name.text;
            v.loc = name.loc;
            return v;
        };
        if (isPunct("++") || isPunct("--")) {
            std::string op = peek().text.substr(0, 1);
            ++i;
            expectPunct(";", "after `" + name.text + op + op + "`");
            Expr one;
            one.kind = Expr::Kind::IntLit;
            one.text = "1";
            one.loc = name.loc;
            Expr bin;
            bin.kind = Expr::Kind::Binary;
            bin.text = op;
            bin.loc = name.loc;
            bin.kids.push_back(var());
            bin.kids.push_back(std::move(one));
            return makeAssign(name, std::move(bin));
        }
        std::string op = peek().text;
        ++i;
        Expr rhs = parseExpr();
        expectPunct(";", "after the assignment to `" + name.text + "`");
        if (op != "=") {  // compound: x += e  →  x = x + e
            Expr bin;
            bin.kind = Expr::Kind::Binary;
            bin.text = op.substr(0, 1);
            bin.loc = name.loc;
            bin.kids.push_back(var());
            bin.kids.push_back(std::move(rhs));
            rhs = std::move(bin);
        }
        return makeAssign(name, std::move(rhs));
    }

    bool atAssignment() const {
        if (peek().kind != Token::Kind::Ident || peek2().kind != Token::Kind::Punct) return false;
        const std::string& t = peek2().text;
        return t == "=" || t == "+=" || t == "-=" || t == "*=" || t == "/=" || t == "%=" ||
               t == "++" || t == "--";
    }

    // The body of `if` / `else`: a braced block or one statement.
    std::vector<Stmt> parseBody() {
        if (isPunct("{")) return parseBlock();
        if (isPunct(";")) {  // `if (c) ;`
            ++i;
            return {};
        }
        if (isIdent("int") || isIdent("unsigned"))
            failHere("a declaration cannot be the only statement of an `if`/`else`",
                     "its scope would end immediately; C itself rejects it.",
                     "wrap it in braces or drop it.");
        std::vector<Stmt> body;
        body.push_back(parseStmt());
        return body;
    }

    std::vector<Stmt> parseBlock() {
        expectPunct("{", "to open a block");
        std::vector<Stmt> body = parseStmtList(false);
        expectPunct("}", "to close the block");
        return body;
    }

    Stmt parseIf() {
        Stmt s;
        s.kind = Stmt::Kind::If;
        s.loc = peek().loc;
        ++i;  // `if`
        s.expr = parseParenExpr("if");
        s.body = parseBody();
        size_t save = i;  // an `else` may follow after comments
        skipComments();
        if (isIdent("else")) {
            ++i;
            s.elseBody = parseBody();
        } else {
            i = save;  // those comments belong to whatever comes next
        }
        return s;
    }

    // `case` labels are integer literals (enum constants arrive with enums).
    Expr parseCaseLabel() {
        Expr e = parseUnary();
        bool lit = e.kind == Expr::Kind::IntLit ||
                   (e.kind == Expr::Kind::UnaryNeg && e.kids[0].kind == Expr::Kind::IntLit);
        if (!lit)
            fail(file, e.loc, "a `case` label must be an integer literal",
                 "named constants and expressions in labels arrive with enums and macros.",
                 "write the number (`case 3:` or `case -1:`).");
        return e;
    }

    Stmt parseSwitch() {
        Stmt s;
        s.kind = Stmt::Kind::Switch;
        s.loc = peek().loc;
        ++i;  // `switch`
        s.expr = parseParenExpr("switch");
        expectPunct("{", "to open the switch body");
        while (true) {
            skipComments();
            if (isPunct("}")) break;
            if (!isIdent("case") && !isIdent("default"))
                failHere("expected `case` or `default`, found " + describe(peek()),
                         "a switch body is a list of `case`/`default` groups.",
                         "put statements under a `case N:` label.");
            Case c;
            c.loc = peek().loc;
            while (true) {  // consecutive labels share one group
                if (isIdent("case")) {
                    ++i;
                    c.labels.push_back(parseCaseLabel());
                } else {
                    ++i;  // `default`
                    c.isDefault = true;
                }
                expectPunct(":", "after the label");
                size_t save = i;
                skipComments();
                if (isIdent("case") || isIdent("default")) continue;
                i = save;  // those comments belong to the first statement
                break;
            }
            c.body = parseStmtList(true);
            while (true) {  // `case 1: { … break; }` and `case 1: { … } break;` are common styles
                if (c.body.size() == 1 && c.body[0].kind == Stmt::Kind::Block) {
                    Stmt block = std::move(c.body[0]);  // the case body is its own scope anyway
                    c.body = std::move(block.body);
                    if (!c.body.empty())
                        c.body[0].comments.insert(c.body[0].comments.begin(),
                                                  block.comments.begin(), block.comments.end());
                } else if (!c.endsInBreak && !c.body.empty() &&
                           c.body.back().kind == Stmt::Kind::Break) {
                    c.body.pop_back();
                    c.endsInBreak = true;
                } else {
                    break;
                }
            }
            s.cases.push_back(std::move(c));
        }
        expectPunct("}", "to close the switch body");
        return s;
    }

    Stmt parseStmt() {
        Stmt s;
        s.loc = peek().loc;
        if (isPunct("{")) {
            s.kind = Stmt::Kind::Block;
            s.body = parseBlock();
            return s;
        }
        if (peek().kind != Token::Kind::Ident)
            failHere("expected a statement, found " + describe(peek()), kSliceWhy,
                     "see spec/impl-spec.md for the statements that are translated.");
        const std::string w = peek().text;
        if (w == "return") {
            ++i;
            s.kind = Stmt::Kind::Return;
            s.expr = parseExpr();
            expectPunct(";", "after the return expression");
            return s;
        }
        if (w == "int" || w == "unsigned") {
            s.type = parseCType();
            Token name = expectName("a variable name");
            checkName(name);
            s.kind = Stmt::Kind::Decl;
            s.name = name.text;
            s.loc = name.loc;
            if (isPunct(";") || isPunct(","))
                fail(file, name.loc, "local `" + name.text + "` has no initial value",
                     "Lean has no uninitialised variables, and picking a value would be guessing (D5).",
                     "give it one: `" + s.type + " " + name.text + " = 0;`.");
            expectPunct("=", "after the name of the local `" + name.text + "`");
            s.expr = parseExpr();
            expectPunct(";", "after the declaration of `" + name.text + "`");
            return s;
        }
        if (w == "if") return parseIf();
        if (w == "switch") return parseSwitch();
        if (w == "break") {
            ++i;
            expectPunct(";", "after `break`");
            s.kind = Stmt::Kind::Break;
            return s;
        }
        if (kLaterStatements.count(w))
            failHere("`" + w + "` is not translated in this slice",
                     "loops arrive in a later slice (see the plan in spec/impl-spec.md).",
                     "write the computation without loops for now.");
        if (kUntranslated.count(w))
            failHere("type `" + w + "` is not translated in this slice",
                     "`" + w + "` exists in the main spec but is not translated yet.",
                     "use `int` for now; the type table grows slice by slice.");
        if (atAssignment()) return parseAssignment();
        if (peek2().kind == Token::Kind::Punct && peek2().text == "(")
            failHere("call statement `" + w + "(…);` has no effect here",
                     "functions are pure in Lean; a discarded result does nothing "
                     "(effects arrive with `printf` and IO).",
                     "use the value: `int r = " + w + "(…);`.");
        failHere("expected a statement, found " + describe(peek()), kSliceWhy,
                 "see spec/impl-spec.md for the statements that are translated.");
    }

    // Statements up to the closing `}` (or, in a switch, the next label).
    std::vector<Stmt> parseStmtList(bool inSwitch) {
        std::vector<Stmt> list;
        while (true) {
            std::vector<Comment> cs = takeComments();
            if (atEnd() || isPunct("}")) break;
            if (inSwitch && (isIdent("case") || isIdent("default"))) break;
            if (isPunct(";")) {  // empty statement
                ++i;
                continue;
            }
            Stmt s = parseStmt();
            s.comments = std::move(cs);
            list.push_back(std::move(s));
        }
        return list;
    }

    // A `break` that is not the closing statement of a switch group.
    void rejectStrayBreak(const std::vector<Stmt>& list) {
        for (const Stmt& s : list) {
            if (s.kind == Stmt::Kind::Break)
                fail(file, s.loc, "`break` is only translated as the last statement of a switch case",
                     "without loops, a `break` elsewhere would need an early exit that is not built yet.",
                     "restructure with `if`/`else`, or end the case with `break;`.");
            rejectStrayBreak(s.body);
            rejectStrayBreak(s.elseBody);
            for (const Case& c : s.cases) rejectStrayBreak(c.body);
        }
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
            std::string ty = parseCType();
            Token name = expectName("a parameter name");
            checkName(name);
            params.emplace_back(ty, name.text);
            if (isPunct(",")) {
                ++i;
                continue;
            }
            break;
        }
        return params;
    }

    // `[[lean::partial]]` (main-spec §3.1). Returns true for `partial`.
    bool parseAttribute() {
        Loc at = peek().loc;
        ++i;
        ++i;  // `[[`
        Token ns = expectIdent("an attribute namespace");
        if (ns.text != "lean")
            fail(file, ns.loc, "unknown attribute namespace `" + ns.text + "`",
                 "clean only knows `lean::…` attributes.", "write `[[lean::partial]]`.");
        expectPunct(":", "in `lean::…`");
        expectPunct(":", "in `lean::…`");
        Token name = expectIdent("an attribute name");
        expectPunct("]", "to close the attribute");
        expectPunct("]", "to close the attribute");
        if (name.text == "partial") return true;
        if (name.text == "inline" || name.text == "nosynth" || name.text == "name")
            fail(file, at, "attribute `lean::" + name.text + "` is not translated yet",
                 "only `lean::partial` exists in this build.", "remove it for now.");
        fail(file, name.loc, "unknown attribute `lean::" + name.text + "`",
             "main-spec §3.1 lists partial, inline, nosynth, name.", "fix the spelling.");
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
        f.body = parseStmtList(false);
        expectPunct("}", "to close the body of `" + f.name + "`");
        rejectStrayBreak(f.body);
        return f;
    }
};

}  // namespace

TranslationUnit parse(const std::vector<Token>& tokens,
                      const std::set<std::string>& definedNames,
                      const std::string& fileName) {
    Parser p{tokens, definedNames, fileName, 0};
    TranslationUnit unit;
    unit.fileName = fileName;

    std::vector<Comment> pending;  // comments waiting for the next definition
    while (true) {
        for (Comment& c : p.takeComments()) pending.push_back(std::move(c));
        if (p.atEnd()) break;
        if (p.isPunct(";")) {  // stray semicolon at file scope (C23 allows it)
            ++p.i;
            continue;
        }
        bool isPartial = false;
        Loc attrLoc = p.peek().loc;
        bool hasAttr = false;
        while (p.isPunct("[") && p.peek2().kind == Token::Kind::Punct && p.peek2().text == "[") {
            isPartial = p.parseAttribute() || isPartial;
            hasAttr = true;
            for (Comment& c : p.takeComments()) pending.push_back(std::move(c));
        }
        if (p.peek().kind != Token::Kind::Ident)
            p.failHere("expected a declaration, found " + describe(p.peek()), kSliceWhy,
                       "files contain `int` and `unsigned` functions; see spec/impl-spec.md.");
        int defLine = p.peek().loc.line;
        std::string retType = p.parseCType();
        Token name = p.expectName("a function name");
        p.checkName(name);
        p.expectPunct("(", "to open the parameter list of `" + name.text + "`");
        auto params = p.parseParams();
        p.expectPunct(")", "to close the parameter list of `" + name.text + "`");
        if (p.isPunct(";")) {  // prototype: dropped (§7.1)
            if (hasAttr)
                fail(fileName, attrLoc, "an attribute on the prototype of `" + name.text + "`",
                     "prototypes are dropped, so the attribute would be silently lost.",
                     "put the attribute on the definition.");
            ++p.i;
            continue;
        }
        Function f = p.parseFunction(name.text, defLine, std::move(params), std::move(pending));
        f.isPartial = isPartial;
        f.retType = retType;
        unit.functions.push_back(std::move(f));
        pending.clear();
    }
    return unit;
}
