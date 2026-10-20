// parser.cpp — implementation of parser.h.
//
// Recursive descent over the grammar in parser.h. It only builds the tree and
// refuses what it cannot build; whether names exist, arities match and every
// path returns is check.cpp's job. Two small desugarings happen here because
// they are purely syntactic: `x += e` → `x = x + e`, and `x++;` → `x = x + 1`.
//
// Comments are not grammar: before parsing, `parse` lifts them out of the
// token stream, each remembering the position it sat at, so a comment can
// never be a syntax error. They are handed back out by position (see the
// attachment rule in parser.h / spec/impl-spec.md) and travel with the tree
// to the emitter.

#include "parser.h"

#include <cstdlib>
#include <utility>

#include "format.h"

namespace {

const char* kSliceWhy =
    "clean translates integer functions built from locals, assignments, `if`, "
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
    "Float", "Float32", "BitVec", "Array", "List", "Option", "true", "false", "decide",
    "Int8", "Int16", "Int32", "Int64", "UInt8", "UInt16", "UInt32", "UInt64", "use",
};

// The C output functions clean turns into `IO` (main-spec §8.1).
bool isPrintName(const std::string& w) {
    return w == "printf" || w == "puts" || w == "putchar" || w == "fprintf";
}

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
    case Token::Kind::IntLit:
    case Token::Kind::FloatLit:
    case Token::Kind::CharLit:
    case Token::Kind::StringLit: return "literal `" + t.text + "`";
    case Token::Kind::Comment: return "a comment";  // unreachable: comments are lifted out before parsing
    case Token::Kind::Punct:   return "`" + t.text + "`";
    case Token::Kind::Other:   return "`" + t.text + "`";
    }
    return "";
}

// Binary operator precedence, loosest first (the C table).
int binaryPrec(const std::string& op) {
    if (op == "||") return 1;
    if (op == "&&") return 2;
    if (op == "|") return 3;
    if (op == "^") return 4;
    if (op == "&") return 5;
    if (op == "==" || op == "!=") return 6;
    if (op == "<" || op == "<=" || op == ">" || op == ">=") return 7;
    if (op == "<<" || op == ">>") return 8;
    if (op == "+" || op == "-") return 9;
    if (op == "*" || op == "/" || op == "%") return 10;
    return 0;
}

Expr::Kind binaryKind(const std::string& op) {
    if (op == "||") return Expr::Kind::Or;
    if (op == "&&") return Expr::Kind::And;
    if (binaryPrec(op) == 6 || binaryPrec(op) == 7) return Expr::Kind::Cmp;
    return Expr::Kind::Binary;
}

struct Parser {
    // `toks` carries no Comment tokens (they were lifted out); `comments`
    // holds each lifted one with the index of the token it sat before.
    const std::vector<Token>& toks;
    std::vector<std::pair<size_t, Comment>> comments;  // (position, comment), in order
    const std::set<std::string>& macros;
    const std::map<std::string, std::string>& macroValues;  // `#define N 8` (bv widths only)
    const std::string& file;
    size_t i = 0;
    size_t nextComment = 0;  // first comment not handed out yet

    const Token& peek() const { return toks[i]; }
    const Token& peek2() const { return toks[i + 1]; }  // safe unless peek() is End
    bool atEnd() const { return peek().kind == Token::Kind::End; }
    bool isPunct(const std::string& s) const {
        return peek().kind == Token::Kind::Punct && peek().text == s;
    }
    bool isIdent(const std::string& s) const {
        return peek().kind == Token::Kind::Ident && peek().text == s;
    }

    // All still-unattached comments that sat before token `pos` (with
    // `through`, also the ones directly before it — i.e. everything up to and
    // including what the parser is looking at right now). Callers hand the
    // result to the nearest statement / definition / block end.
    std::vector<Comment> takeComments(size_t pos, bool through) {
        std::vector<Comment> out;
        while (nextComment < comments.size() &&
               (through ? comments[nextComment].first <= pos
                        : comments[nextComment].first < pos)) {
            out.push_back(std::move(comments[nextComment++].second));
        }
        return out;
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

    // Is the next token a word of a C type (supported or not yet translated)?
    bool atTypeWord() const {
        return peek().kind == Token::Kind::Ident &&
               classifyTypeWord(peek().text) != TypeWord::NotAType;
    }

    // The `N` of `bv(N)` / `BV(N, v)`: an integer literal, or a
    // `#define NAME <integer literal>`; anything else is E302.
    long long parseWidthValue() {
        long long n = -1;
        if (peek().kind == Token::Kind::IntLit) {
            n = std::strtoll(peek().text.c_str(), nullptr, 0);  // decimal or 0x…
            if (peek().text.size() > 1 && peek().text[1] == 'b') n = -1;
            ++i;
        } else if (peek().kind == Token::Kind::Ident && macroValues.count(peek().text)) {
            n = std::strtoll(macroValues.at(peek().text).c_str(), nullptr, 0);
            ++i;
        }
        if (n < 0)
            failCode("E302", file, peek().loc, "the width of a bit-vector is not a constant I can read",
                     "Lean's `BitVec N` needs the width at translation time.",
                     "write a decimal literal, or `#define NAME 8` (a plain integer) and use `bv(NAME)`.");
        return n;
    }

    Type bitVecType(long long n, Loc at) {
        TypeResolution r = makeBitVec(n);
        if (!r.ok) failCode(r.code.c_str(), file, at, r.problem, r.why, r.fix);
        return r.type;
    }

    Type parseBitVec() {
        Loc at = peek().loc;
        ++i;  // `bv`
        expectPunct("(", "after `bv`");
        long long n = parseWidthValue();
        expectPunct(")", "to close `bv(…)`");
        return bitVecType(n, at);
    }

    // `[N]` / `[]` after a name: the size (a literal or `#define NAME <integer>`), -1 when left out.
    long long parseArraySize() {
        expectPunct("[", "to open the array size");
        long long n = -1;
        if (!isPunct("]")) {
            if (peek().kind == Token::Kind::IntLit && !(peek().text.size() > 1 && peek().text[1] == 'b')) {
                n = std::strtoll(peek().text.c_str(), nullptr, 0);
                ++i;
            } else if (peek().kind == Token::Kind::Ident && macroValues.count(peek().text)) {
                n = std::strtoll(macroValues.at(peek().text).c_str(), nullptr, 0);
                ++i;
            } else {
                fail(file, peek().loc, "the size of an array is not a constant I can read",
                     "variable-length arrays have no Lean counterpart, and the size is not part of the type.",
                     "write a decimal literal, or `#define NAME 8` (a plain integer) and use `[NAME]`.");
            }
            if (n < 1)
                fail(file, peek().loc, "an array needs at least one element", "C rejects `a[0]` too.",
                     "write a positive size.");
        }
        expectPunct("]", "to close the array size");
        return n;
    }

    // The `*`s and `[N]`s after a type, applied to it (types.cpp decides what they mean).
    Type declare(Type base, int stars, size_t brackets, Loc at, bool isParam) {
        TypeResolution r = applyDeclarator(base, stars, (int)brackets, isParam);
        if (!r.ok) failCode(r.code.c_str(), file, at, r.problem, r.why, r.fix);
        return r.type;
    }

    int parseStars() {
        int n = 0;
        while (isPunct("*")) {
            ++n;
            ++i;
            while (isIdent("const") || isIdent("restrict")) ++i;  // `char *const p`
        }
        return n;
    }

    // One C type: all consecutive type words (`unsigned long long int`),
    // resolved by types.cpp, or `bv(N)`. Knows no type by name itself.
    Type parseCType() {
        if (!atTypeWord())
            failHere("expected a type, found " + describe(peek()),
                     "only " + supportedTypesHint() + " are translated in this slice.",
                     "write " + supportedTypesHint() + "; the type table grows slice by slice.");
        Loc at = peek().loc;
        if (isBitVecWord(peek().text)) return parseBitVec();
        std::vector<std::string> words;
        while (atTypeWord() && !isBitVecWord(peek().text)) words.push_back(toks[i++].text);
        TypeResolution r = resolveTypeWords(words);
        if (!r.ok) failCode(r.code.c_str(), file, at, r.problem, r.why, r.fix);
        return r.type;
    }

    // A name in declaration position (`int int f()`, `unsigned unsigned x = 1;`).
    // Type words are rejected or the emitter would print a C keyword where the
    // output needs an identifier.
    Token expectName(const std::string& ctx) {
        if (atTypeWord())
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
        if (isPunct("(") && peek2().kind == Token::Kind::Ident &&
            classifyTypeWord(peek2().text) != TypeWord::NotAType) {  // a cast `(T)x`
            Loc at = peek().loc;
            ++i;
            Type target = parseCType();
            if (isPunct("*"))
                failCode("E101", file, peek().loc, "a cast to a pointer type",
                         "main-spec §4.6 accepts no pointer casts.", "remove the cast.");
            expectPunct(")", "to close the cast");
            Expr e;
            e.kind = Expr::Kind::Cast;
            e.type = target;
            e.loc = at;
            e.kids.push_back(parseUnary());
            return e;
        }
        if (isPunct("-") || isPunct("!") || isPunct("~")) {
            Token op = toks[i++];
            Expr e;
            e.kind = op.text == "-" ? Expr::Kind::UnaryNeg
                     : op.text == "~" ? Expr::Kind::BitNot : Expr::Kind::Not;
            e.loc = op.loc;
            e.kids.push_back(parseUnary());
            return e;
        }
        Expr e = parsePrimary();
        while (isPunct("[")) {  // `a[i]`, `a[i][j]`
            ++i;
            Expr ix;
            ix.kind = Expr::Kind::Index;
            ix.loc = e.loc;
            Expr idx = parseExpr();
            expectPunct("]", "to close the index");
            ix.kids.push_back(std::move(e));
            ix.kids.push_back(std::move(idx));
            e = std::move(ix);
        }
        if (isPunct("++") || isPunct("--")) failIncDec();
        return e;
    }

    // `BV(8, 0x63)` → the literal `0x63#8` (main-spec §5.4).
    Expr parseBvLiteral() {
        Loc at = peek().loc;
        i += 2;  // `BV` `(`
        Type t = bitVecType(parseWidthValue(), at);
        expectPunct(",", "between the width and the value of `BV(…)`");
        if (peek().kind != Token::Kind::IntLit)
            failHere("the value of `BV(…)` must be an integer literal",
                     "Lean's `v#N` literal takes a numeral.", "write e.g. `BV(8, 0x63)`.");
        Expr e;
        e.kind = Expr::Kind::BvLit;
        e.text = peek().text;
        e.type = t;
        e.loc = at;
        ++i;
        expectPunct(")", "to close `BV(…)`");
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
        if (t.kind == Token::Kind::StringLit) {
            Expr e;
            e.kind = Expr::Kind::StringLit;
            e.text = t.text;
            e.loc = t.loc;
            ++i;
            while (peek().kind == Token::Kind::StringLit) {  // "a" "b" is one literal in C
                e.text = e.text.substr(0, e.text.size() - 1) + peek().text.substr(1);
                ++i;
            }
            return e;
        }
        if (t.kind == Token::Kind::FloatLit || t.kind == Token::Kind::CharLit) {
            ++i;
            Expr e;
            e.kind = t.kind == Token::Kind::FloatLit ? Expr::Kind::FloatLit : Expr::Kind::CharLit;
            e.text = t.text;
            e.loc = t.loc;
            return e;
        }
        if (t.kind == Token::Kind::Ident && (t.text == "true" || t.text == "false")) {
            ++i;
            Expr e;
            e.kind = Expr::Kind::BoolLit;
            e.text = t.text;
            e.loc = t.loc;
            return e;
        }
        if (t.kind == Token::Kind::Ident && t.text == "BV" && peek2().kind == Token::Kind::Punct &&
            peek2().text == "(")
            return parseBvLiteral();
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

    // `x = e;`  `x += e;`  `x++;`  and the element forms `a[i][j] = e;` `a[i] += e;` `a[i]++;`
    Stmt parseAssignment() {
        Token name = toks[i++];
        std::vector<Expr> indices;
        while (isPunct("[")) {
            ++i;
            indices.push_back(parseExpr());
            expectPunct("]", "to close the index");
        }
        auto var = [&]() {  // the left-hand side read back: `a`, or `a[i][j]`
            Expr v;
            v.kind = Expr::Kind::Ident;
            v.text = name.text;
            v.loc = name.loc;
            for (const Expr& idx : indices) {
                Expr ix;
                ix.kind = Expr::Kind::Index;
                ix.loc = name.loc;
                ix.kids.push_back(std::move(v));
                ix.kids.push_back(idx);
                v = std::move(ix);
            }
            return v;
        };
        auto assign = [&](Expr rhs) {
            Stmt s = makeAssign(name, std::move(rhs));
            s.indices = indices;
            return s;
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
            return assign(std::move(bin));
        }
        std::string op = peek().text;
        ++i;
        Expr rhs = parseExpr();
        expectPunct(";", "after the assignment to `" + name.text + "`");
        if (op != "=") {  // compound: x += e  →  x = x + e
            Expr bin;
            bin.kind = Expr::Kind::Binary;
            bin.text = op.substr(0, op.size() - 1);
            bin.loc = name.loc;
            bin.kids.push_back(var());
            bin.kids.push_back(std::move(rhs));
            rhs = std::move(bin);
        }
        return assign(std::move(rhs));
    }

    static bool isAssignOp(const Token& t) {
        if (t.kind != Token::Kind::Punct) return false;
        const std::string& x = t.text;
        return x == "=" || x == "+=" || x == "-=" || x == "*=" || x == "/=" || x == "%=" ||
               x == "&=" || x == "|=" || x == "^=" || x == "<<=" || x == ">>=" || x == "++" || x == "--";
    }

    // `name` then an assignment operator, or `name[…]…[…]` then one.
    bool atAssignment() const {
        if (peek().kind != Token::Kind::Ident) return false;
        size_t j = i + 1;
        while (toks[j].kind == Token::Kind::Punct && toks[j].text == "[") {
            int depth = 0;
            for (; toks[j].kind != Token::Kind::End; ++j) {
                if (toks[j].kind == Token::Kind::Punct && toks[j].text == "[") ++depth;
                if (toks[j].kind == Token::Kind::Punct && toks[j].text == "]" && --depth == 0) break;
            }
            if (toks[j].kind == Token::Kind::End) return false;
            ++j;
        }
        return isAssignOp(toks[j]);
    }

    // `{1, 2, 3}` / `{{1, 2}, {3, 4}}`: the initialiser of an array local.
    Expr parseInitList() {
        Expr e;
        e.kind = Expr::Kind::ArrayLit;
        e.loc = peek().loc;
        expectPunct("{", "to open the initialiser list");
        while (!isPunct("}")) {
            e.kids.push_back(isPunct("{") ? parseInitList() : parseExpr());
            if (!isPunct(",")) break;
            ++i;
        }
        expectPunct("}", "to close the initialiser list");
        return e;
    }

    // The body of `if` / `else`: a braced block or one statement. `trailing`
    // receives the comments that sat just before the closing `}` (empty for
    // the one-statement form — there is no `}` there).
    std::vector<Stmt> parseBody(std::vector<Comment>& trailing) {
        if (isPunct("{")) return parseBlock(trailing);
        if (isPunct(";")) {  // `if (c) ;`
            ++i;
            return {};
        }
        if (atTypeWord())
            failHere("a declaration cannot be the only statement of an `if`/`else`",
                     "its scope would end immediately; C itself rejects it.",
                     "wrap it in braces or drop it.");
        std::vector<Stmt> body;
        body.push_back(parseStmt());
        return body;
    }

    std::vector<Stmt> parseBlock(std::vector<Comment>& trailing) {
        expectPunct("{", "to open a block");
        std::vector<Stmt> body = parseStmtList(false);
        expectPunct("}", "to close the block");
        trailing = takeComments(i, false);  // the comments that ended this block
        return body;
    }

    Stmt parseIf() {
        Stmt s;
        s.kind = Stmt::Kind::If;
        s.loc = peek().loc;
        ++i;  // `if`
        s.expr = parseParenExpr("if");
        for (Comment& c : takeComments(i, true)) s.comments.push_back(std::move(c));
        s.body = parseBody(s.bodyTrailing);
        // Comments between the body and a following `else` are attached by
        // the `else` body's first statement (they precede its first token),
        // so there is no lookahead dance here; without an `else` they simply
        // belong to whatever statement comes next.
        if (isIdent("else")) {
            ++i;
            s.elseBody = parseBody(s.elseTrailing);
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
        for (Comment& c : takeComments(i, true)) s.comments.push_back(std::move(c));
        expectPunct("{", "to open the switch body");
        while (true) {
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
                if (isIdent("case") || isIdent("default")) continue;
                break;  // comments before the body are picked up by its first statement
            }
            c.body = parseStmtList(true);
            while (true) {  // `case 1: { … break; }` and `case 1: { … } break;` are common styles
                if (c.body.size() == 1 && c.body[0].kind == Stmt::Kind::Block) {
                    Stmt block = std::move(c.body[0]);  // the case body is its own scope anyway
                    c.body = std::move(block.body);
                    if (!c.body.empty())
                        c.body[0].comments.insert(c.body[0].comments.begin(),
                                                  block.comments.begin(), block.comments.end());
                    c.bodyTrailing.insert(c.bodyTrailing.end(),
                                          block.bodyTrailing.begin(), block.bodyTrailing.end());
                } else if (!c.endsInBreak && !c.body.empty() &&
                           c.body.back().kind == Stmt::Kind::Break) {
                    Stmt br = std::move(c.body.back());
                    c.body.pop_back();
                    c.endsInBreak = true;
                    // a comment above the stripped `break;` stays at the end of the group
                    c.bodyTrailing.insert(c.bodyTrailing.end(),
                                          br.comments.begin(), br.comments.end());
                } else {
                    break;
                }
            }
            s.cases.push_back(std::move(c));
        }
        expectPunct("}", "to close the switch body");
        // a comment at the very end of the switch body stays with the last group
        if (!s.cases.empty())
            for (Comment& c : takeComments(i, false)) s.cases.back().bodyTrailing.push_back(std::move(c));
        return s;
    }

    // `printf(fmt, args);` `fprintf(stderr, fmt, args);` `puts(s);` `putchar(c);` — main-spec §8.
    // The format must be a string literal: it is cut into pieces here (format.h) because
    // the Lean `s!"…"` is built at translation time. Which argument fits which conversion
    // is check.cpp's question; how many there are is answered here.
    Stmt parsePrint() {
        Stmt s;
        s.kind = Stmt::Kind::Print;
        s.loc = peek().loc;
        s.name = toks[i++].text;
        expectPunct("(", "after `" + s.name + "`");
        const bool formatted = s.name == "printf" || s.name == "fprintf";
        if (s.name == "fprintf") {
            if (peek().kind == Token::Kind::Ident && (peek().text == "stderr" || peek().text == "stdout")) {
                s.toStderr = peek().text == "stderr";
                ++i;
            } else {
                failHere("`fprintf` to something other than `stderr` or `stdout`",
                         "writing to files needs `fopen` and `IO.FS`, which main-spec §8.1 keeps 🟡.",
                         "print to `stdout` or `stderr`.");
            }
            expectPunct(",", "after the stream of `fprintf`");
        }
        if (formatted) {
            if (peek().kind != Token::Kind::StringLit)
                failHere("the format of `" + s.name + "` is not a string literal",
                         "clean builds the Lean `s!\"…\"` while translating, so it must see the format.",
                         "write the format as a literal: `printf(\"%s\", text)` prints a string variable.");
            Loc at = peek().loc;
            std::string text = toks[i++].text;
            while (peek().kind == Token::Kind::StringLit) {  // "a" "b" is one literal in C
                text = text.substr(0, text.size() - 1) + peek().text.substr(1);
                ++i;
            }
            FormatResult f = parseFormat(text.substr(1, text.size() - 2));
            if (!f.ok) fail(file, at, f.problem, f.why, f.fix);
            s.pieces = std::move(f.pieces);
            s.newline = f.newline;
            while (isPunct(",")) {
                ++i;
                s.args.push_back(parseExpr());
            }
            size_t convs = 0;
            for (const FmtPiece& p : s.pieces) convs += p.conv != 0;
            if (convs != s.args.size())
                fail(file, at,
                     "the format of `" + s.name + "` has " + std::to_string(convs) + " conversion(s) but " +
                         std::to_string(s.args.size()) + " argument(s) follow",
                     "each conversion consumes one argument; C leaves a mismatch undefined.",
                     "make the number of arguments match the conversions.");
        } else {
            s.args.push_back(parseExpr());
        }
        expectPunct(")", "to close the argument list of `" + s.name + "`");
        expectPunct(";", "after `" + s.name + "(…)`");
        return s;
    }

    Stmt parseStmt() {
        Stmt s;
        s.loc = peek().loc;
        if (isPunct("{")) {
            s.kind = Stmt::Kind::Block;
            s.body = parseBlock(s.bodyTrailing);
            return s;
        }
        if (peek().kind != Token::Kind::Ident)
            failHere("expected a statement, found " + describe(peek()), kSliceWhy,
                     "see spec/impl-spec.md for the statements that are translated.");
        const std::string w = peek().text;
        if (w == "return") {
            ++i;
            s.kind = Stmt::Kind::Return;
            if (isPunct(";")) {  // `return;` (a `void` function; check.cpp matches it to the signature)
                s.noValue = true;
                ++i;
                return s;
            }
            s.expr = parseExpr();
            expectPunct(";", "after the return expression");
            return s;
        }
        if (classifyTypeWord(w) == TypeWord::Supported) {
            Loc typeAt = peek().loc;
            Type base = parseCType();
            int stars = parseStars();
            Token name = expectName("a variable name");
            checkName(name);
            s.kind = Stmt::Kind::Decl;
            s.name = name.text;
            s.loc = name.loc;
            if (base.kind == Type::Kind::Unit && stars == 0)
                fail(file, typeAt, "a variable of type `void`", "`void` has no values.", "use a real type.");
            while (isPunct("[")) s.dims.push_back(parseArraySize());
            s.type = declare(base, stars, s.dims.size(), typeAt, false);
            if (isPunct(",") || (isPunct(";") && s.dims.empty()))
                fail(file, name.loc, "local `" + name.text + "` has no initial value",
                     "Lean has no uninitialised variables, and picking a value would be guessing (D5).",
                     "give it an initial value, e.g. `= 0`.");
            if (isPunct(";")) {  // `T a[N];` — arrays are filled with default values (§4.4)
                if (s.type.kind != Type::Kind::Array)
                    fail(file, name.loc, "local `" + name.text + "` has no initial value",
                         "Lean strings are immutable, so an empty `char s[N]` could never be filled.",
                         "write the string: `char s[] = \"…\";`.");
                for (long long d : s.dims)
                    if (d < 0)
                        fail(file, name.loc, "the size of the array `" + name.text + "` is missing",
                             "with no initialiser there is nothing to take the size from.",
                             "write the size: `" + name.text + "[N]`.");
                s.noInit = true;
                ++i;
                return s;
            }
            expectPunct("=", "after the name of the local `" + name.text + "`");
            if (s.type.kind == Type::Kind::Array) {
                if (!isPunct("{"))
                    failHere("an array is initialised with `{ … }`",
                             "C cannot copy an array with `=`.", "write the initialiser list.");
                s.expr = parseInitList();
            } else {
                if (isPunct("{") && s.type.kind == Type::Kind::String)
                    failHere("a `char` array is a `String` in Lean",
                             "main-spec §4.3 maps `char s[]` to an immutable `String`, not to an array of characters.",
                             "initialise it with a string literal, or use `uint8_t a[N] = { … }` for bytes.");
                s.expr = parseExpr();
            }
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
        if (atTypeWord()) parseCType();  // a type clean does not translate: refused with the reason
        if (isPrintName(w) && peek2().kind == Token::Kind::Punct && peek2().text == "(") return parsePrint();
        if (atAssignment()) return parseAssignment();
        if (peek2().kind == Token::Kind::Punct && peek2().text == "(") {  // `f(args);` (check.cpp: void only)
            s.kind = Stmt::Kind::CallStmt;
            s.expr = parseExpr();
            if (s.expr.kind != Expr::Kind::Call)
                fail(file, s.expr.loc, "an expression statement has no effect",
                     "functions are pure in Lean, so only a call to a `void` function can stand alone.",
                     "use the value, e.g. assign it to a local.");
            expectPunct(";", "after the call");
            return s;
        }
        failHere("expected a statement, found " + describe(peek()), kSliceWhy,
                 "see spec/impl-spec.md for the statements that are translated.");
    }

    // Statements up to the closing `}` (or, in a switch, the next label).
    // The break checks come first so that a comment before a `}` or a label is
    // left for whoever owns that position (the block, the next group) — only
    // comments that really precede a statement are taken here.
    std::vector<Stmt> parseStmtList(bool inSwitch) {
        std::vector<Stmt> list;
        while (true) {
            if (atEnd() || isPunct("}")) break;
            if (inSwitch && (isIdent("case") || isIdent("default"))) break;
            std::vector<Comment> cs = takeComments(i, true);
            if (isPunct(";")) {  // empty statement
                ++i;
                continue;
            }
            Stmt s = parseStmt();
            std::vector<Comment> inner = takeComments(i, false);  // comments inside the statement
            for (Comment& c : s.comments) cs.push_back(std::move(c));
            for (Comment& c : inner) cs.push_back(std::move(c));
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

    std::vector<std::pair<Type, std::string>> parseParams() {
        std::vector<std::pair<Type, std::string>> params;
        if (isIdent("void") && peek2().kind == Token::Kind::Punct && peek2().text == ")") {
            ++i;  // `void`
            return params;  // the `)` stays for the caller
        }
        if (isPunct(")")) return params;  // `f()`
        std::vector<std::pair<size_t, Loc>> pointers;  // `T *a` parameters: they need a length after them
        while (true) {
            Loc at = peek().loc;
            Type base = parseCType();
            int stars = parseStars();
            Token name = expectName("a parameter name");
            checkName(name);
            if (base.kind == Type::Kind::Unit && stars == 0)
                fail(file, at, "a parameter of type `void`", "`void` has no values.", "use a real type.");
            size_t brackets = 0;
            while (isPunct("[")) {
                parseArraySize();  // the size is not part of the type (§4.4)
                ++brackets;
            }
            Type ty = declare(base, stars, brackets, at, true);
            if (stars == 1 && ty.kind == Type::Kind::Array) pointers.emplace_back(params.size(), at);
            params.emplace_back(ty, name.text);
            if (isPunct(",")) {
                ++i;
                continue;
            }
            break;
        }
        // §4.6: pointer decay is accepted only as `T *a, size_t n`.
        for (const auto& pr : pointers) {
            bool ok = pr.first + 1 < params.size();
            if (ok) {
                const Type& next = params[pr.first + 1].first;
                ok = next.kind == Type::Kind::Nat || next.kind == Type::Kind::Int ||
                     next.kind == Type::Kind::UInt || next.kind == Type::Kind::SInt;
            }
            if (!ok)
                failCode("E101", file, pr.second, "pointer parameter `" + params[pr.first].second + "` has no length",
                         "main-spec §4.6 accepts `T *a` only together with a length parameter right after it.",
                         "write `T a[]`, or add `size_t n` after it.");
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
                           std::vector<std::pair<Type, std::string>> params,
                           std::vector<Comment> comments) {
        Function f;
        f.name = std::move(name);
        f.defLine = defLine;
        f.params = std::move(params);
        f.comments = std::move(comments);
        for (Comment& c : takeComments(i, true)) f.comments.push_back(std::move(c));
        expectPunct("{", "to open the body of `" + f.name + "`");
        f.body = parseStmtList(false);
        expectPunct("}", "to close the body of `" + f.name + "`");
        f.bodyTrailing = takeComments(i, false);  // the comments that ended the body
        rejectStrayBreak(f.body);
        return f;
    }
};

}  // namespace

TranslationUnit parse(const std::vector<Token>& tokens,
                      const std::set<std::string>& definedNames,
                      const std::map<std::string, std::string>& definedValues,
                      const std::string& fileName) {
    // Lift the comments out of the token stream: the grammar below never sees
    // one, so a comment can sit anywhere C allows without becoming a syntax
    // error. Each remembers the index of the token it sat before, so it can be
    // handed back to whatever that position belongs to (statement, definition,
    // block end — the rule is documented in parser.h).
    std::vector<Token> toks;
    std::vector<std::pair<size_t, Comment>> comments;
    for (const Token& t : tokens) {
        if (t.kind == Token::Kind::Comment)
            comments.push_back({toks.size(), Comment{t.text, t.block}});
        else
            toks.push_back(t);
    }

    Parser p{toks, comments, definedNames, definedValues, fileName, 0};
    TranslationUnit unit;
    unit.fileName = fileName;

    std::vector<Comment> pending;  // comments waiting for the next definition
    while (true) {
        for (Comment& c : p.takeComments(p.i, true)) pending.push_back(std::move(c));
        if (p.atEnd()) {  // comments after the last definition end the file
            unit.trailingComments = std::move(pending);
            break;
        }
        if (p.isPunct(";")) {  // stray semicolon at file scope (C23 allows it)
            ++p.i;
            continue;
        }
        if (p.peek().kind == Token::Kind::Ident && isPrintName(p.peek().text) &&
            p.peek2().kind == Token::Kind::Punct && p.peek2().text == "(") {  // script mode (§8.2)
            Stmt st = p.parsePrint();
            st.script = true;
            st.comments = std::move(pending);
            pending.clear();
            for (Comment& c : p.takeComments(p.i, false)) st.comments.push_back(std::move(c));
            unit.scripts.push_back(std::move(st));
            continue;
        }
        bool isPartial = false;
        Loc attrLoc = p.peek().loc;
        bool hasAttr = false;
        while (p.isPunct("[") && p.peek2().kind == Token::Kind::Punct && p.peek2().text == "[") {
            isPartial = p.parseAttribute() || isPartial;
            hasAttr = true;
            for (Comment& c : p.takeComments(p.i, true)) pending.push_back(std::move(c));
        }
        if (p.peek().kind != Token::Kind::Ident)
            p.failHere("expected a declaration, found " + describe(p.peek()), kSliceWhy,
                       "files contain functions; see spec/impl-spec.md.");
        int defLine = p.peek().loc.line;
        Loc retAt = p.peek().loc;
        Type retType = p.parseCType();
        retType = p.declare(retType, p.parseStars(), 0, retAt, false);
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
