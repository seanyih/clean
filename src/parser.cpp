// parser.cpp — implementation of parser.h.
//
// Recursive descent over the grammar in parser.h. It only builds the tree and
// refuses what it cannot build; whether names exist, arities match and every
// path returns is check.cpp's job. Small desugarings happen here because they
// are purely syntactic: `x += e` → `x = x + e`, `x++;` → `x = x + 1`, and a
// `for` header with no condition gets a literal `true`. Loops are parsed here
// in their C shape (`While` / `DoWhile` / `For`); which `for` becomes a Lean
// range and which one becomes a `while` is check.cpp's decision (it needs the
// types). `break`/`continue` legality is checked here too: the walk knows
// only the shape of the tree, which is all the question needs.
//
// Comments are not grammar: before parsing, `parse` lifts them out of the
// token stream, each remembering the position it sat at, so a comment can
// never be a syntax error. They are handed back out by position (see the
// attachment rule in parser.h / spec/impl-spec.md) and travel with the tree
// to the emitter.

#include "parser.h"

#include <cstdlib>
#include <utility>

#include "builtins.h"
#include "format.h"

namespace {

const char* kSliceWhy =
    "clean translates integer functions built from locals, assignments, `if`, "
    "`switch`, loops (`for`, `while`, `do`) and `return`; other statements and "
    "types arrive slice by slice (see spec/impl-spec.md).";

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
    // tokens Lean 4.35 reserves that are plausible C names (each probed with `def NAME : Nat := 1`)
    "at", "end", "where", "using", "deriving", "instance", "class", "section", "extends", "local",
    "generalizing", "calc", "nomatch", "suffices", "notation", "macro", "syntax", "universe",
    "set_option", "attribute", "export", "mutual", "protected", "noncomputable", "unsafe",
    "termination_by", "decreasing_by", "omit", "include", "renaming", "hiding", "prefix", "infix",
    "postfix", "opaque", "elab",
};


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
    std::map<std::string, Type> typedefs;  // `typedef T name;` seen so far: name → T (its `alias` = name)
    std::map<std::string, Type> enumTags;  // `enum Tag { … }` seen so far: Tag → its type
    std::map<std::string, Type> structTags;  // `struct Tag { … }` seen so far: Tag → its type
    bool typeBodies = false;               // file scope: `enum … { … }` may be defined here
    std::vector<TypeDecl> newDecls;        // type declarations met while parsing one file-scope item
    std::vector<Loc> longDoubles;          // where `long double` was written (lint W003)

    // An integer literal's value (`7`, `0x1F`, `0b11`).
    static long long literalValue(const std::string& t) {
        if (t.size() > 2 && t[0] == '0' && (t[1] == 'b' || t[1] == 'B'))
            return (long long)std::strtoull(t.c_str() + 2, nullptr, 2);
        return (long long)std::strtoull(t.c_str(), nullptr, 0);
    }

    // `struct Tag` (defined above), or a definition `struct [Tag] { T f; T g[N]; … }`
    // at file scope (main-spec §4.5), queued in `newDecls` like an enum.
    Type parseStructType() {
        Loc at = peek().loc;
        ++i;  // `struct`
        std::string tag;
        Loc tagAt = peek().loc;
        if (!isPunct("{")) {
            Token t = expectName("the name of the `struct`");
            checkName(t);
            tag = t.text;
        }
        if (!isPunct("{")) {
            auto it = structTags.find(tag);
            if (it == structTags.end())
                fail(file, tagAt, "`struct " + tag + "` is not defined above this point",
                     "Lean needs the `structure` before its first use; a struct that points to itself "
                     "(a linked list) is a pointer, which main-spec §4.6 refuses.",
                     "define it first: `struct " + tag + " { … };`.");
            return it->second;
        }
        if (!typeBodies)
            fail(file, at, "a `struct` is defined inside a function, a parameter list or another struct",
                 "Lean's `structure` is a file-scope declaration.",
                 "define the `struct` at file scope, above its use.");
        if (structTags.count(tag))
            fail(file, tagAt, "`struct " + tag + "` is defined twice", "C rejects it too.", "rename one of them.");
        TypeDecl d;
        d.kind = TypeDecl::Kind::Struct;
        d.name = tag;
        d.line = at.line;
        d.loc = tag.empty() ? at : tagAt;
        ++i;  // `{`
        typeBodies = false;  // a field's type cannot define a new struct/enum
        while (!isPunct("}")) {
            for (Comment& c : takeComments(i, true)) d.comments.push_back(std::move(c));
            if (isPunct("}")) break;
            Loc ft = peek().loc;
            Type base = parseCType();
            while (true) {  // `int x, y;` declares two fields
                int stars = parseStars();
                Token n = expectName("a field name");
                checkName(n);
                Field f;
                f.name = n.text;
                f.loc = n.loc;
                while (isPunct("[")) f.dims.push_back(parseArraySize());
                for (long long dim : f.dims)
                    if (dim < 0)
                        fail(file, n.loc, "a field array without a size",
                             "a flexible array member has no fixed size, and its storage is a pointer trick.",
                             "write the size: `" + n.text + "[N]`.");
                if (isPunct(":"))
                    failCode("E111", file, peek().loc, "a bit-field `" + n.text + " : …`",
                             "Lean structures have no bit-fields (main-spec §4.5).",
                             "use a bit-vector field: `bv(N) " + n.text + ";`.");
                f.type = declare(base, stars, f.dims.size(), ft, false);
                if (f.type.kind == Type::Kind::Unit || f.type.kind == Type::Kind::File)
                    fail(file, n.loc, "a field of type `" + cName(f.type) + "`",
                         f.type.kind == Type::Kind::File ? "a stream is not a value a structure can hold "
                                                           "(it has no `Repr`/`BEq`)."
                                                         : "`void` has no values.",
                         "pass it as a separate parameter.");
                if (f.type.kind == Type::Kind::String && !f.dims.empty() && stars == 0) f.dims.clear();  // `char s[16]`
                for (const Field& g : d.fields)
                    if (g.name == f.name)
                        fail(file, n.loc, "field `" + f.name + "` is declared twice", "C rejects it too.",
                             "rename one of them.");
                d.fields.push_back(std::move(f));
                if (!isPunct(",")) break;
                ++i;
            }
            expectPunct(";", "after the field");
        }
        ++i;  // `}`
        typeBodies = true;
        if (d.fields.empty())
            fail(file, at, "a `struct` with no fields", "C rejects it too.", "add a field.");
        Type t = Type::named(Type::Kind::Struct, tag);
        if (!tag.empty()) structTags[tag] = t;
        newDecls.push_back(std::move(d));
        return t;
    }

    // `enum Tag` (defined above), or a definition `enum [Tag] { A, B = 3, C }`
    // at file scope (main-spec §4.5). A definition is queued in `newDecls`; an
    // anonymous one gets its name from the `typedef` around it.
    Type parseEnumType() {
        Loc at = peek().loc;
        ++i;  // `enum`
        std::string tag;
        Loc tagAt = peek().loc;
        if (!isPunct("{")) {
            Token t = expectName("the name of the `enum`");
            checkName(t);
            tag = t.text;
        }
        if (!isPunct("{")) {
            auto it = enumTags.find(tag);
            if (it == enumTags.end())
                fail(file, tagAt, "`enum " + tag + "` is not defined above this point",
                     "Lean needs the `inductive` before its first use, and clean does not guess its enumerators.",
                     "define it first: `enum " + tag + " { … };`.");
            return it->second;
        }
        if (!typeBodies)
            fail(file, at, "an `enum` is defined inside a function or a parameter list",
                 "Lean's `inductive` is a file-scope declaration.", "define the `enum` at file scope, above its use.");
        if (enumTags.count(tag))
            fail(file, tagAt, "`enum " + tag + "` is defined twice", "C rejects it too.", "rename one of them.");
        TypeDecl d;
        d.kind = TypeDecl::Kind::Enum;
        d.name = tag;
        d.line = at.line;
        d.loc = tag.empty() ? at : tagAt;
        ++i;  // `{`
        long long next = 0;
        while (!isPunct("}")) {
            Token n = expectName("an enumerator");
            checkName(n);
            Enumerator en{n.text, next, n.loc};
            if (isPunct("=")) {
                ++i;
                if (isPunct("-"))
                    failHere("a negative enumerator value",
                             "the `E.toNat` that gives an enumerator its value returns a `Nat` (main-spec §4.5).",
                             "use non-negative values.");
                if (peek().kind != Token::Kind::IntLit)
                    failHere("an enumerator value must be an integer literal",
                             "clean reads the value while translating; expressions are not evaluated.",
                             "write the number: `" + n.text + " = 3`.");
                en.value = literalValue(toks[i++].text);
                d.explicitValues = true;
            }
            d.enumerators.push_back(en);
            next = en.value + 1;
            if (!isPunct(",")) break;
            ++i;
        }
        expectPunct("}", "to close the `enum`");
        if (d.enumerators.empty())
            fail(file, at, "an `enum` with no enumerators", "C rejects it too.", "list at least one.");
        Type t = Type::named(Type::Kind::Enum, tag);
        if (!tag.empty()) enumTags[tag] = t;
        newDecls.push_back(std::move(d));
        return t;
    }

    const Token& peek() const { return toks[i]; }
    const Token& peek2() const { return toks[i + 1]; }  // safe unless peek() is End
    bool atEnd() const { return peek().kind == Token::Kind::End; }
    bool isPunct(const std::string& s) const {
        return peek().kind == Token::Kind::Punct && peek().text == s;
    }
    bool isPunctAt(size_t j, const std::string& s) const {
        return j < toks.size() && toks[j].kind == Token::Kind::Punct && toks[j].text == s;
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

    // Is token `j` a word of a C type (supported or not yet translated), or a
    // `typedef` name?
    bool isTypeToken(size_t j) const {
        return toks[j].kind == Token::Kind::Ident &&
               (classifyTypeWord(toks[j].text) != TypeWord::NotAType || typedefs.count(toks[j].text) ||
                toks[j].text == "enum" || toks[j].text == "struct");
    }
    bool atTypeWord() const { return isTypeToken(i); }

    // Does a declaration start here: a type clean translates, or a `typedef` name?
    bool atDeclStart() const {
        return peek().kind == Token::Kind::Ident &&
               (classifyTypeWord(peek().text) == TypeWord::Supported || typedefs.count(peek().text) ||
                peek().text == "enum" || peek().text == "struct");
    }
    static bool isQualifier(const Token& t) {
        return t.kind == Token::Kind::Ident && (t.text == "const" || t.text == "restrict");
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
        size_t j = i;  // `const byte`, `byte const`: qualifiers around a typedef name are ignored (§4.7)
        while (isQualifier(toks[j])) ++j;
        if (toks[j].kind == Token::Kind::Ident && typedefs.count(toks[j].text)) {
            i = j + 1;
            while (isQualifier(peek())) ++i;
            return typedefs.at(toks[j].text);
        }
        if (isQualifier(peek()) && toks[j].kind == Token::Kind::Ident &&
            (toks[j].text == "enum" || toks[j].text == "struct"))
            i = j;
        if (isIdent("enum")) return parseEnumType();
        if (isIdent("struct")) return parseStructType();
        if (isBitVecWord(peek().text)) return parseBitVec();
        std::vector<std::string> words;
        while (atTypeWord() && !isBitVecWord(peek().text)) words.push_back(toks[i++].text);
        bool isLong = false, isDouble = false;
        for (const std::string& w : words) {
            isLong = isLong || w == "long";
            isDouble = isDouble || w == "double";
        }
        if (isLong && isDouble) longDoubles.push_back(at);  // `long double` → `Float` (lint W003)
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
        Expr e = parseTernary();
        if (isPunct("="))  // `if (x = 1)`, `return x = 2;`, `f(a = b)`
            failCode("E103", file, peek().loc, "assignment used as an expression",
                     "Lean has no assignment expressions; assignments are statements.",
                     "split it into a separate statement.");
        return e;
    }

    // `c ? a : b` (main-spec §5.1): looser than `||`, right-associative, so
    // `a ? b : c ? d : e` is `a ? b : (c ? d : e)` as in C.
    Expr parseTernary() {
        Expr c = parseBinary(1);
        if (!isPunct("?")) return c;
        Expr e;
        e.kind = Expr::Kind::Cond;
        e.loc = c.loc;
        ++i;  // `?`
        Expr a = parseExpr();
        expectPunct(":", "between the branches of `?:`");
        Expr b = parseTernary();
        e.kids.push_back(std::move(c));
        e.kids.push_back(std::move(a));
        e.kids.push_back(std::move(b));
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

    [[noreturn]] void failFunctionPointer() {
        failCode("E107", file, peek().loc, "a function pointer",
                 "function values are ⛔ in v0.1 (main-spec §4.6; 🟡 as function-typed parameters).",
                 "call the function by name, or pass a value that selects it (an `enum`) and `switch` on it.");
    }

    [[noreturn]] void failArrow() {
        failCode("E101", file, peek().loc, "`->` reads a field through a pointer",
                 "main-spec §4.6 has no pointers to structs; Lean structures are values.",
                 "pass the struct itself and write `p.x`.");
    }

    [[noreturn]] void failIncDec() {
        failCode("E110", file, peek().loc, "`" + peek().text + "` inside an expression",
                 "an increment has a side effect, and Lean expressions have none.",
                 "write it as a separate statement (`x++;` or `x += 1;`).");
    }

    Expr parseUnary() {
        if (isPunct("++") || isPunct("--")) failIncDec();
        if (isPunct("(") && isTypeToken(i + 1)) {  // a cast `(T)x`
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
        while (isPunct("[") || isPunct(".") || isPunct("->")) {  // `a[i]`, `a[i][j]`, `p.x`, `a[i].x`
            if (isPunct("->")) failArrow();
            if (isPunct(".")) {
                ++i;
                Token f = expectIdent("a field name after `.`");
                Expr m;
                m.kind = Expr::Kind::Member;
                m.text = f.text;
                m.loc = e.loc;
                m.kids.push_back(std::move(e));
                e = std::move(m);
                continue;
            }
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

    // `LEAN_DECL("…")`, `LEAN_STMT("…")`, `LEAN_EXPR("…")` (main-spec §11.1): the argument is a string
    // literal (adjacent ones join) holding Lean source, which is passed through as written.
    std::string parseRawLean(const std::string& macro) {
        ++i;  // the macro name
        expectPunct("(", "after `" + macro + "`");
        if (peek().kind != Token::Kind::StringLit)
            failHere("`" + macro + "` takes a string literal", "its argument is Lean source, passed through as written.",
                     "write `" + macro + "(\"…\")`.");
        std::string text;
        while (peek().kind == Token::Kind::StringLit) {
            const std::string& lit = toks[i++].text;  // Lean spelling, quotes included: undo the escapes
            for (size_t k = 1; k + 1 < lit.size(); ++k) {
                if (lit[k] != '\\' || k + 2 >= lit.size()) {
                    text += lit[k];
                    continue;
                }
                const char n = lit[++k];
                text += n == 'n' ? '\n' : n == 't' ? '\t' : n;
            }
        }
        expectPunct(")", "to close `" + macro + "`");
        return text;
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
        if (t.kind == Token::Kind::Ident && (t.text == "sizeof" || t.text == "_Alignof" || t.text == "alignof"))
            failCode("E112", file, t.loc, "`" + t.text + "` is not translated",
                     "sizes are a property of C's memory layout; Lean arrays carry their length instead.",
                     "pass the size explicitly (`int a[], size_t n`), or use the `#define` of the size.");
        if (t.kind == Token::Kind::Ident && t.text == "LEAN_EXPR" && peek2().kind == Token::Kind::Punct &&
            peek2().text == "(") {
            Expr e;
            e.kind = Expr::Kind::Raw;
            e.loc = t.loc;
            e.text = parseRawLean("LEAN_EXPR");
            return e;
        }
        if (t.kind == Token::Kind::Ident && isLibraryConstant(t.text)) {  // EXIT_FAILURE, EOF, …
            ++i;
            const std::string v = libraryConstant(t.text);
            Expr e;
            e.kind = Expr::Kind::IntLit;
            e.text = v[0] == '-' ? v.substr(1) : v;
            e.loc = t.loc;
            if (v[0] != '-') return e;
            Expr neg;  // written as the `-1` C's `EOF` stands for
            neg.kind = Expr::Kind::UnaryNeg;
            neg.loc = t.loc;
            neg.kids.push_back(std::move(e));
            return neg;
        }
        if (t.kind == Token::Kind::Ident) {
            // `#define max(a, b) …` is the idiom main-spec §12.2 maps to Lean's `min`/`max`
            const bool minMaxIdiom = stdShape(t.text) == StdShape::MinMax && peek2().kind == Token::Kind::Punct &&
                                     peek2().text == "(";
            if (macros.count(t.text) && !minMaxIdiom)
                fail(file, t.loc,
                     "macro `" + t.text + "` is used, but macros are not expanded in this slice",
                     "a macro name is not a Lean name; expanding it would be guessing.",
                     "write the value directly, or wait for preprocessor support.");
            ++i;
            if (isPunct("(") && classifyLibName(t.text) == LibName::Scan)
                fail(file, t.loc, "the result of `" + t.text + "(…)` is used as a value",
                     "`" + t.text + "` is translated as an `IO` action that stores into variables; the count "
                     "of conversions it returns is not given back, and a variable keeps its value when "
                     "nothing was read.",
                     "call it as a statement, on its own line, and give the variables a default value.");
            if (isPunct("(")) {  // function call
                ++i;
                Expr e;
                e.kind = Expr::Kind::Call;
                e.text = t.text;
                e.loc = t.loc;
                auto argument = [&]() {  // a width / bit position of a §5.4 helper may be a `#define`d number
                    if (bitVecConstArg(t.text, e.kids.size()) && peek().kind == Token::Kind::Ident &&
                        macroValues.count(peek().text)) {
                        Expr n;
                        n.kind = Expr::Kind::IntLit;
                        n.text = macroValues.at(peek().text);
                        n.loc = peek().loc;
                        ++i;
                        return n;
                    }
                    return parseExpr();
                };
                if (!isPunct(")")) {
                    e.kids.push_back(argument());
                    while (isPunct(",")) {
                        ++i;
                        e.kids.push_back(argument());
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
    // `semi` = the statement form wants its `;`; a `for` step does not (a `)` follows).
    Stmt parseAssignment(bool semi) {
        Token name = toks[i++];
        std::vector<PathStep> path;  // `[i]` and `.f` steps below the variable
        bool viaField = false;
        while (isPunct("[") || isPunct(".") || isPunct("->")) {
            if (isPunct("->")) failArrow();
            PathStep st;
            if (isPunct(".")) {
                ++i;
                st.field = true;
                st.name = expectIdent("a field name after `.`").text;
                viaField = true;
            } else {
                ++i;
                st.index = parseExpr();
                expectPunct("]", "to close the index");
            }
            path.push_back(std::move(st));
        }
        auto var = [&]() {  // the left-hand side read back: `a`, `a[i][j]`, `p.a[i].x`
            Expr v;
            v.kind = Expr::Kind::Ident;
            v.text = name.text;
            v.loc = name.loc;
            for (const PathStep& st : path) {
                Expr ix;
                ix.kind = st.field ? Expr::Kind::Member : Expr::Kind::Index;
                ix.loc = name.loc;
                if (st.field) ix.text = st.name;
                ix.kids.push_back(std::move(v));
                if (!st.field) ix.kids.push_back(st.index);
                v = std::move(ix);
            }
            return v;
        };
        auto assign = [&](Expr rhs) {
            Stmt s = makeAssign(name, std::move(rhs));
            if (viaField) {
                s.path = path;
            } else {
                for (const PathStep& st : path) s.indices.push_back(st.index);
            }
            return s;
        };
        if (isPunct("++") || isPunct("--")) {
            std::string op = peek().text.substr(0, 1);
            ++i;
            if (semi) expectPunct(";", "after `" + name.text + op + op + "`");
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
        if (semi) expectPunct(";", "after the assignment to `" + name.text + "`");
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
        while (toks[j].kind == Token::Kind::Punct && (toks[j].text == "[" || toks[j].text == ".")) {
            if (toks[j].text == ".") {  // `.field`
                if (toks[j + 1].kind != Token::Kind::Ident) return false;
                j += 2;
                continue;
            }
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
            if (isPunct(".")) {  // `.x = e`: a designated struct field (main-spec §4.5)
                ++i;
                Token f = expectIdent("a field name after `.`");
                if (isPunct(".") || isPunct("["))
                    failHere("a nested designator", "clean reads one field name per designator.",
                             "write the inner value as its own list: `.p = { .x = 1 }`.");
                expectPunct("=", "after the designator `." + f.text + "`");
                e.fields.resize(e.kids.size());
                e.fields.push_back(f.text);
            } else if (isPunct("[")) {
                failHere("an array designator `[i] = …`", "clean reads array initialisers in order.",
                         "list the elements in order.");
            } else if (!e.fields.empty()) {
                e.fields.push_back("");
            }
            e.kids.push_back(isPunct("{") ? parseInitList() : parseExpr());
            if (!isPunct(",")) break;
            ++i;
        }
        expectPunct("}", "to close the initialiser list");
        if (!e.fields.empty()) {
            e.fields.resize(e.kids.size());
            for (const std::string& f : e.fields)
                if (f.empty())
                    fail(file, e.loc, "a list mixes designated (`.x = …`) and positional values",
                         "clean reads a struct initialiser either by name or in order.",
                         "name every field, or none.");
        }
        return e;
    }

    // The body of `if` / `else` / a loop: a braced block or one statement. `trailing`
    // receives the comments that sat just before the closing `}` (empty for
    // the one-statement form — there is no `}` there).
    std::vector<Stmt> parseBody(std::vector<Comment>& trailing) {
        if (isPunct("{")) return parseBlock(trailing);
        if (isPunct(";")) {  // `if (c) ;`, `while (c) ;`
            ++i;
            return {};
        }
        if (atTypeWord())
            failHere("a declaration cannot be the only statement of an `if`/`else`/loop body",
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
        bool lit = e.kind == Expr::Kind::IntLit || e.kind == Expr::Kind::Ident ||  // an enumerator (check.cpp)
                   (e.kind == Expr::Kind::UnaryNeg && e.kids[0].kind == Expr::Kind::IntLit);
        if (!lit)
            fail(file, e.loc, "a `case` label must be an integer literal or an enumerator",
                 "expressions in labels are not evaluated, and macros are not expanded.",
                 "write the number (`case 3:` or `case -1:`), or the enumerator.");
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

    // ---- loops (main-spec §6) ------------------------------------------------

    // `while (expr) body` — the condition and the body are checked like an
    // `if`'s; the shape is the same in Lean.
    Stmt parseWhile() {
        Stmt s;
        s.kind = Stmt::Kind::While;
        s.loc = peek().loc;
        ++i;  // `while`
        s.expr = parseParenExpr("while");
        for (Comment& c : takeComments(i, true)) s.comments.push_back(std::move(c));
        s.body = parseBody(s.bodyTrailing);
        return s;
    }

    // `do body while (expr);` — the body runs first, so check.cpp emits it as
    // `repeat body; unless expr do break`.
    Stmt parseDoWhile() {
        Stmt s;
        s.kind = Stmt::Kind::DoWhile;
        s.loc = peek().loc;
        ++i;  // `do`
        s.body = parseBody(s.bodyTrailing);
        for (Comment& c : takeComments(i, true))  // between `}` and `while`: at the body's end
            s.bodyTrailing.push_back(std::move(c));
        if (!isIdent("while"))
            failHere("expected `while` to close the `do`, found " + describe(peek()),
                     "C requires `do { … } while (…);`.", "write the `while (…);` part.");
        ++i;
        s.expr = parseParenExpr("while");
        for (Comment& c : takeComments(i, true)) s.comments.push_back(std::move(c));
        expectPunct(";", "after the `do … while`");
        return s;
    }

    // The `f(args)` statement without its `;` (a `for` header piece; the `)`
    // follows). The caller has seen IDENT `(`.
    Stmt parseCallNoSemi() {
        Stmt s;
        s.kind = Stmt::Kind::CallStmt;
        s.loc = peek().loc;
        s.expr = parseExpr();
        if (s.expr.kind != Expr::Kind::Call)
            fail(file, s.expr.loc, "an expression statement has no effect",
                 "functions are pure in Lean, so only a call to a `void` function can stand alone.",
                 "use the value, e.g. assign it to a local.");
        return s;
    }

    // One assignment-shaped `for` step, without a `;`: `i++`, `++i`, `i = …`,
    // `i += …`, `a[i] = …`. The prefix forms are legal only here.
    Stmt parseForStep() {
        if (isPunct("++") || isPunct("--")) {
            Loc at = peek().loc;
            std::string op = peek().text.substr(0, 1);
            ++i;
            Token name = expectName("the variable after `" + op + op + "`");
            checkName(name);
            Expr one;
            one.kind = Expr::Kind::IntLit;
            one.text = "1";
            one.loc = at;
            Expr bin;
            bin.kind = Expr::Kind::Binary;
            bin.text = op;
            bin.loc = at;
            Expr lhs;
            lhs.kind = Expr::Kind::Ident;
            lhs.text = name.text;
            lhs.loc = name.loc;
            bin.kids.push_back(std::move(lhs));
            bin.kids.push_back(std::move(one));
            Stmt s = makeAssign(name, std::move(bin));
            s.loc = at;
            return s;
        }
        if (atAssignment()) return parseAssignment(false);
        if (peek().kind == Token::Kind::Ident && peek2().kind == Token::Kind::Punct &&
            peek2().text == "(")
            return parseCallNoSemi();
        failHere("the step of a `for` is an assignment or a call",
                 "C allows any expression there; clean has only statements.",
                 "write `i++`, `i += e`, `i = e` or a call.");
    }

    // C's comma in a `for` header (`for (i = 0, j = 1; …; i++, j++)`): the
    // catalogue's E103. Called after a header piece, when it ends early.
    void noComma(const std::string& part) {
        if (isPunct(","))
            failCode("E103", file, peek().loc, "the comma operator in a `for` " + part,
                     "C evaluates both sides for their side effects; clean has no expressions "
                     "with effects (E103).",
                     "declare one variable, or move the rest into the loop body.");
    }

    // `for (init?; cond?; step?) body` (§6.1). The three parts are kept as
    // parsed; check.cpp decides range-for vs the general `while` form.
    Stmt parseFor() {
        Stmt s;
        s.kind = Stmt::Kind::For;
        s.loc = peek().loc;
        ++i;  // `for`
        expectPunct("(", "after `for`");
        if (!isPunct(";")) {  // the initialiser: a declaration, an assignment or a call
            if (atDeclStart())
                s.init.push_back(parseDeclTail());  // eats the `;` that separates the parts
            else if (atAssignment()) {
                Stmt a = parseAssignment(false);
                noComma("initialiser");
                expectPunct(";", "after the assignment");
                s.init.push_back(std::move(a));
            } else if (peek().kind == Token::Kind::Ident && peek2().kind == Token::Kind::Punct &&
                       peek2().text == "(") {
                Stmt c = parseCallNoSemi();
                noComma("initialiser");
                expectPunct(";", "after the call");
                s.init.push_back(std::move(c));
            } else {
                failHere("the initialiser of a `for` is a declaration, an assignment or a call",
                         "C allows any expression there; clean has only statements, and the "
                         "comma operator is ⛔ (E103).",
                         "write `T i = e`, `i = e`, or a call; move the rest into the loop body.");
            }
        } else {
            ++i;  // the `;` of an absent initialiser
        }
        if (!isPunct(";")) {  // the condition: absent means `true` (C: loop forever)
            s.expr = parseExpr();
            expectPunct(";", "after the condition of the `for`");
        } else {
            ++i;
            s.expr.kind = Expr::Kind::BoolLit;  // `for (;;)` — the general form emits `while true`
            s.expr.text = "true";
            s.expr.loc = s.loc;
        }
        if (!isPunct(")")) {
            s.step.push_back(parseForStep());
            noComma("step");
        }
        expectPunct(")", "to close the header of the `for`");
        for (Comment& c : takeComments(i, true)) s.comments.push_back(std::move(c));
        s.body = parseBody(s.bodyTrailing);
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
        const bool toString = s.name == "sprintf" || s.name == "snprintf";
        const bool formatted = s.name == "printf" || s.name == "fprintf" || toString;
        if (toString) {  // the string variable that receives the text, and (snprintf) its size
            Token target = expectIdent("the string variable `" + s.name + "` fills");
            s.targets.push_back(target.text);
            s.targetLocs.push_back(target.loc);
            expectPunct(",", "after the buffer of `" + s.name + "`");
            if (s.name == "snprintf") {
                s.hasLimit = true;
                s.limit = parseExpr();
                expectPunct(",", "after the size of `snprintf`");
            }
        }
        if (s.name == "fprintf") {
            if (peek().kind == Token::Kind::Ident && (peek().text == "stderr" || peek().text == "stdout") &&
                peek2().kind == Token::Kind::Punct && peek2().text == ",") {
                s.toStderr = peek().text == "stderr";  // the two standard streams keep their own spelling
                ++i;
            } else {
                s.hasStream = true;  // any `FILE *` value (check.cpp: it must be one)
                s.stream = parseExpr();
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
            if (s.name == "fputs" || s.name == "fputc" || s.name == "putc") {  // the stream comes last
                expectPunct(",", "after the value of `" + s.name + "`");
                s.hasStream = true;
                s.stream = parseExpr();
            }
        }
        expectPunct(")", "to close the argument list of `" + s.name + "`");
        expectPunct(";", "after `" + s.name + "(…)`");
        return s;
    }

    // The format of `scanf` / `fscanf`: a string literal, cut up like a `printf` one. Only
    // whitespace may sit between the conversions (what C does with it is "skip any whitespace",
    // which is what every conversion but `%c` does anyway); literal text to match is refused.
    // Width, flags and precision change what is read (`%5s`), so they are refused too.
    void parseScanFormat(Stmt& s) {
        if (peek().kind != Token::Kind::StringLit)
            failHere("the format of `" + s.name + "` is not a string literal",
                     "clean needs to see which conversions read which variables.",
                     "write the format as a literal: `scanf(\"%d\", &n)`.");
        Loc at = peek().loc;
        std::string text = toks[i++].text;
        while (peek().kind == Token::Kind::StringLit) {  // "a" "b" is one literal in C
            text = text.substr(0, text.size() - 1) + peek().text.substr(1);
            ++i;
        }
        FormatResult f = parseFormat(text.substr(1, text.size() - 2));
        if (!f.ok) fail(file, at, f.problem, f.why, f.fix);
        for (const FmtPiece& p : f.pieces) {
            if (p.conv) {
                if (p.padded)
                    fail(file, at, "a width, flag or precision in the format of `" + s.name + "`",
                         "`%5s` and friends change how much is read, and clean reads whole words.",
                         "drop it.");
                if (p.conv == 'X' || p.conv == 'x')
                    fail(file, at, "`%x` in the format of `" + s.name + "`",
                         "reading hexadecimal is not translated.", "read a decimal number with `%d`.");
                continue;
            }
            for (size_t k = 0; k < p.lit.size(); ++k) {
                const char c = p.lit[k];
                const bool ws = c == ' ' || c == '\t' ||
                                (c == '\\' && k + 1 < p.lit.size() && (p.lit[k + 1] == 'n' || p.lit[k + 1] == 't'));
                if (c == '\\' && ws) {
                    ++k;
                    continue;
                }
                if (!ws)
                    fail(file, at, "literal text `" + p.lit + "` in the format of `" + s.name + "`",
                         "C would have to match it exactly in the input; clean reads whitespace-separated words.",
                         "read everything with conversions separated by spaces, and handle the text yourself.");
            }
        }
        s.pieces = std::move(f.pieces);
        s.newline = f.newline;
    }

    // `scanf(fmt, &a, &b);` and `fscanf(f, fmt, &a);` — each conversion reads into a variable. The
    // variable is `&x` (`%s` takes the string `x` itself). Which types fit which conversion is check.cpp's.
    Stmt parseScan() {
        Stmt s;
        s.kind = Stmt::Kind::Scan;
        s.loc = peek().loc;
        s.name = toks[i++].text;
        expectPunct("(", "after `" + s.name + "`");
        if (s.name == "fscanf") {
            s.hasStream = true;
            s.stream = parseExpr();
            expectPunct(",", "after the stream of `fscanf`");
        }
        Loc at = peek().loc;
        parseScanFormat(s);
        while (isPunct(",")) {
            ++i;
            s.targetLocs.push_back(peek().loc);
            const bool amp = isPunct("&");
            s.targetAddr.push_back(amp);
            if (amp) ++i;
            if (peek().kind != Token::Kind::Ident)
                failHere("`" + s.name + "` reads into a variable, found " + describe(peek()),
                         "the value has to be stored somewhere: a plain variable, `&x`.",
                         "write `&name` (or just `name` for a `%s` string).");
            s.targets.push_back(toks[i++].text);
            if (isPunct("[") || isPunct("."))
                failHere("`" + s.name + "` reads into an element or a field",
                         "only whole variables can receive a value (`a = f(a)` in Lean).",
                         "read into a local variable, then store it: `a[i] = x;`.");
        }
        size_t convs = 0;
        for (const FmtPiece& p : s.pieces) convs += p.conv != 0;
        if (convs != s.targets.size())
            fail(file, at,
                 "the format of `" + s.name + "` has " + std::to_string(convs) + " conversion(s) but " +
                     std::to_string(s.targets.size()) + " variable(s) follow",
                 "each conversion reads into one variable.", "make the number of variables match the conversions.");
        expectPunct(")", "to close the argument list of `" + s.name + "`");
        expectPunct(";", "after `" + s.name + "(…)`");
        return s;
    }

    // `fgets(s, n, f);` — reads one line (at most n-1 characters, newline kept) into the string `s`.
    Stmt parseReadLine() {
        Stmt s;
        s.kind = Stmt::Kind::ReadLine;
        s.loc = peek().loc;
        ++i;  // `fgets`
        expectPunct("(", "after `fgets`");
        Token target = expectIdent("the string variable `fgets` fills");
        s.name = target.text;
        expectPunct(",", "after the buffer of `fgets`");
        s.args.push_back(parseExpr());
        expectPunct(",", "after the size of `fgets`");
        s.hasStream = true;
        s.stream = parseExpr();
        expectPunct(")", "to close the argument list of `fgets`");
        expectPunct(";", "after `fgets(…)`");
        return s;
    }

    // `fclose(f);`
    Stmt parseClose() {
        Stmt s;
        s.kind = Stmt::Kind::Close;
        s.loc = peek().loc;
        ++i;  // `fclose`
        expectPunct("(", "after `fclose`");
        s.hasStream = true;
        s.stream = parseExpr();
        expectPunct(")", "to close the argument list of `fclose`");
        expectPunct(";", "after `fclose(…)`");
        return s;
    }

    // `exit(code);` — main-spec §8.1 (`IO.Process.exit`). check.cpp converts the status to the
    // `UInt8` Lean wants, which is C's own "status mod 256".
    Stmt parseExit() {
        Stmt s;
        s.kind = Stmt::Kind::Exit;
        s.loc = peek().loc;
        ++i;  // `exit`
        expectPunct("(", "after `exit`");
        s.expr = parseExpr();
        expectPunct(")", "to close the argument list of `exit`");
        expectPunct(";", "after `exit(…)`");
        return s;
    }

    // The `T name = expr;` / `T name[N]…;` statement, trailing `;` included —
    // also the initialiser piece of a `for` header, where that `;` is the
    // header's separator. Only called when the next word is a supported type.
    Stmt parseDeclTail() {
        Stmt s;
        Loc typeAt = peek().loc;
        Type base = parseCType();
        if (isPunct("(") && isPunctAt(i + 1, "*")) failFunctionPointer();
        int stars = parseStars();
        Token name = expectName("a variable name");
        checkName(name);
        s.kind = Stmt::Kind::Decl;
        s.name = name.text;
        s.loc = name.loc;
        if (base.kind == Type::Kind::Unit && stars == 0)
            fail(file, typeAt, "a variable of type `void`", "`void` has no values.", "use a real type.");
        while (isPunct("[")) s.dims.push_back(parseArraySize());
        if (stars && isPunct("=") && toks[i + 1].kind == Token::Kind::Ident &&
            (toks[i + 1].text == "malloc" || toks[i + 1].text == "calloc" || toks[i + 1].text == "realloc"))
            failCode("E108", file, toks[i + 1].loc, "dynamic memory (`" + toks[i + 1].text + "`) is not translatable",
                     "Lean values are immutable; there is no heap model (main-spec §14.5).",
                     "declare `" + cName(base) + " " + name.text + "[N];` (an Array) and index it.");
        s.type = declare(base, stars, s.dims.size(), typeAt, false);
        if (isPunct(","))
            fail(file, peek().loc, "several variables in one declaration",
                 "each C declarator becomes its own Lean `let`; clean reads one per statement.",
                 "declare one variable per statement: `int a = 0; int b = 0;`.");
        if (isPunct(";") && s.type.kind == Type::Kind::File)
            fail(file, name.loc, "the stream `" + name.text + "` has no initial value",
                 "a Lean stream has no \"nothing\" value to start from (clean has no `NULL`).",
                 "declare it where you open it: `FILE *f = fopen(…);`.");
        if (isPunct(";")) {  // `T x;` — zero (main-spec §4.5, lint W004); `T a[N];` — default values (§4.4)
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
        } else if (s.type.kind == Type::Kind::Struct && isPunct("{")) {
            s.expr = parseInitList();  // `struct P p = {1, 2};` / `{.x = 1}` (main-spec §4.5)
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
        if (atDeclStart()) return parseDeclTail();
        if (w == "if") return parseIf();
        if (w == "switch") return parseSwitch();
        if (w == "while") return parseWhile();
        if (w == "do") return parseDoWhile();
        if (w == "for") return parseFor();
        if (w == "break") {
            ++i;
            expectPunct(";", "after `break`");
            s.kind = Stmt::Kind::Break;
            return s;
        }
        if (w == "continue") {
            ++i;
            expectPunct(";", "after `continue`");
            s.kind = Stmt::Kind::Continue;
            return s;
        }
        if (w == "asm" || w == "__asm__" || w == "__asm")
            failCode("E109", file, peek().loc, "inline assembly is not translatable",
                     "machine code has no Lean meaning (main-spec Appendix A).", "write it in C.");
        if (w == "LEAN_STMT" && peek2().kind == Token::Kind::Punct && peek2().text == "(") {
            s.kind = Stmt::Kind::Raw;
            s.name = parseRawLean("LEAN_STMT");
            expectPunct(";", "after `LEAN_STMT(…)`");
            return s;
        }
        if (w == "goto")
            failCode("E102", file, peek().loc, "`goto` is not translated",
                     "main-spec §6 keeps `goto` and labels ⛔; Lean has nothing they could map to.",
                     "restructure with a loop (and `break`/`continue`), or an `if`/`else` chain.");
        if (peek2().kind == Token::Kind::Punct && peek2().text == ":")
            failCode("E102", file, peek().loc, "label `" + w + "` is not translated",
                     "labels exist only for `goto`, which main-spec §6 keeps ⛔.",
                     "drop the label; restructure with a loop, or an `if`/`else` chain.");
        if (w == "static")
            failCode("E106", file, peek().loc, "a `static` local variable",
                     "it keeps its value between calls, which a pure Lean function cannot; `StateM`/`IO.Ref` "
                     "are 🟡 (main-spec §7.3).",
                     "pass the state in and out as a parameter.");
        if (atTypeWord()) parseCType();  // a type clean does not translate: refused with the reason
        if (peek2().kind == Token::Kind::Punct && peek2().text == "(") {  // a library statement
            switch (classifyLibName(w)) {
            case LibName::Output:   return parsePrint();
            case LibName::Exit:     return parseExit();
            case LibName::Scan:     return parseScan();
            case LibName::ReadLine: return parseReadLine();
            case LibName::Close:    return parseClose();
            case LibName::Value:    break;  // `getchar();` — an expression statement, check.cpp judges it
            case LibName::BitVec:   break;
            case LibName::None:     break;
            }
        }
        if (atAssignment()) return parseAssignment(true);
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

    // `break` / `continue` legality (main-spec §6). Only the shape of the tree
    // matters: a `break` is legal inside a loop — and, inside a `switch` case,
    // only as the case's last statement, which parseSwitch has already
    // stripped; a `continue` is legal inside a loop, even inside a case (C
    // binds it to the loop, and so does the emitted Lean). So the walk tracks
    // two flags: `inLoop` (a loop encloses) and `inCase` (a switch case
    // encloses, and no loop opened inside it — a `break` there would be C's
    // "end the case early", which clean does not translate, not a loop exit).
    void checkJumps(const std::vector<Stmt>& list, bool inLoop, bool inCase) {
        for (const Stmt& s : list) {
            if (s.kind == Stmt::Kind::Break && !(inLoop && !inCase))
                fail(file, s.loc, "`break` is not translated here",
                     inCase ? "C binds this `break` to the enclosing `switch` — it would end "
                              "the case early, and clean's `match` arms cannot do that "
                              "(only the case's last `break`, which the parser strips)."
                            : "a `break` ends the innermost loop, and there is none here.",
                     inCase ? "move it to the end of its case (or drop it), and use the "
                              "loop's own `break` outside the `switch`."
                            : "delete it, or put it inside a loop.");
            if (s.kind == Stmt::Kind::Continue && !inLoop)
                fail(file, s.loc, "`continue` outside a loop",
                     "C binds it to the innermost loop, and there is none here.",
                     "delete it, or put it inside a loop.");
            switch (s.kind) {
            case Stmt::Kind::If:
                checkJumps(s.body, inLoop, inCase);
                checkJumps(s.elseBody, inLoop, inCase);
                break;
            case Stmt::Kind::Block:
                checkJumps(s.body, inLoop, inCase);
                break;
            case Stmt::Kind::Switch:
                for (const Case& c : s.cases) checkJumps(c.body, inLoop, true);
                break;
            case Stmt::Kind::While:
            case Stmt::Kind::DoWhile:
                checkJumps(s.body, true, false);
                break;
            case Stmt::Kind::For:
                checkJumps(s.body, true, false);
                break;
            default:
                break;
            }
        }
    }

    // ---- declarations --------------------------------------------------

    std::vector<std::pair<Type, std::string>> parseParams(bool isMain) {
        std::vector<std::pair<Type, std::string>> params;
        if (isIdent("void") && peek2().kind == Token::Kind::Punct && peek2().text == ")") {
            ++i;  // `void`
            return params;  // the `)` stays for the caller
        }
        if (isPunct(")")) return params;  // `f()`
        std::vector<std::pair<size_t, Loc>> pointers;  // `T *a` parameters: they need a length after them
        while (true) {
            Loc at = peek().loc;
            if (isPunct(".") && isPunctAt(i + 1, ".") && isPunctAt(i + 2, "."))
                failCode("E109", file, at, "a variadic function (`...`)",
                         "Lean functions take a fixed list of arguments; only the `printf` family is translated.",
                         "take the arguments explicitly, or an array.");
            Type base = parseCType();
            if (isPunct("(") && isPunctAt(i + 1, "*")) failFunctionPointer();
            int stars = parseStars();
            Token name;  // a prototype may leave it out (`int f(int);`); a definition may not (checked by the caller)
            name.loc = peek().loc;
            if (!isPunct(",") && !isPunct(")") && !isPunct("[")) {
                name = expectName("a parameter name");
                checkName(name);
            }
            if (base.kind == Type::Kind::Unit && stars == 0)
                fail(file, at, "a parameter of type `void`", "`void` has no values.", "use a real type.");
            size_t brackets = 0;
            while (isPunct("[")) {
                parseArraySize();  // the size is not part of the type (§4.4)
                ++brackets;
            }
            // `main(int argc, char **argv)` / `char *argv[]`: the one place an array of strings exists (§7.1)
            const bool argvForm = isMain && base.kind == Type::Kind::Char &&
                                  ((stars == 2 && brackets == 0) || (stars == 1 && brackets == 1));
            Type ty = argvForm ? Type::arrayOf(Type::of(Type::Kind::String)) : declare(base, stars, brackets, at, true);
            if (stars == 1 && ty.kind == Type::Kind::Array && !argvForm) pointers.emplace_back(params.size(), at);
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
    // Returns the attribute's name: "partial" or "inline".
    std::string parseAttribute() {
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
        if (name.text == "partial" || name.text == "inline") return name.text;
        if (name.text == "nosynth" || name.text == "name")
            fail(file, at, "attribute `lean::" + name.text + "` is not translated yet",
                 "only `lean::partial` and `lean::inline` exist in this build.", "remove it for now.");
        fail(file, name.loc, "unknown attribute `lean::" + name.text + "`",
             "main-spec §3.1 lists partial, inline, nosynth, name.", "fix the spelling.");
    }

    // `typedef T name;` at file scope (main-spec §4.5) → `abbrev name := T`. The
    // name becomes a type word for the rest of the file; values of the type
    // keep its name (`Type::alias`), so the output reads `byte`, not `BitVec 8`.
    void parseTypedef(std::vector<Comment> comments) {
        TypeDecl d;
        d.kind = TypeDecl::Kind::Alias;
        d.line = peek().loc.line;
        d.comments = std::move(comments);
        ++i;  // `typedef`
        Loc at = peek().loc;
        typeBodies = true;  // `typedef enum { … } Color;`
        Type base = parseCType();
        typeBodies = false;
        int stars = parseStars();
        Token name = expectName("the name the `typedef` introduces");
        checkName(name);
        if (isPunct("["))
            failHere("a `typedef` of an array type is not translated",
                     "the size is not part of a Lean `Array` type (D13), so the name would lose it.",
                     "declare the arrays with their size, or typedef the element type.");
        if (isPunct("("))
            failCode("E107", file, peek().loc, "a `typedef` of a function type",
                     "function pointers are ⛔ in v0.1 (main-spec §4.6).", "pass a named function instead.");
        Type t = declare(base, stars, 0, at, false);
        if (t.kind == Type::Kind::Unit)
            fail(file, at, "a `typedef` of `void`", "`void` has no values to name.", "use a real type.");
        if (stars && base.kind == Type::Kind::Struct)
            failCode("E101", file, at, "a `typedef` of a pointer to a struct",
                     "main-spec §4.6 accepts no pointers to structs.", "typedef the struct itself.");
        expectPunct(";", "after the `typedef`");
        if (typedefs.count(name.text))
            fail(file, name.loc, "`" + name.text + "` is defined twice by `typedef`",
                 "Lean allows only one definition per name.", "rename one of them.");
        for (Comment& c : takeComments(i, false)) d.comments.push_back(std::move(c));
        const bool named = t.kind == Type::Kind::Enum || t.kind == Type::Kind::Struct;
        if (named && t.name.empty()) {
            // `typedef enum { … } Color;`: the anonymous type takes the name, no `abbrev` needed
            TypeDecl& def = newDecls.back();
            def.name = name.text;
            def.comments.insert(def.comments.begin(), d.comments.begin(), d.comments.end());
            typedefs[name.text] = Type::named(t.kind, name.text);
            return;
        }
        if (named && t.name == name.text) {  // `typedef enum Color {…} Color;`
            typedefs[name.text] = t;
            if (!newDecls.empty()) newDecls.back().comments.insert(newDecls.back().comments.begin(),
                                                                   d.comments.begin(), d.comments.end());
            return;
        }
        d.name = name.text;
        d.loc = name.loc;
        d.type = t;        // what the abbreviation stands for (printed with its own name, if it has one)
        t.alias = name.text;
        typedefs[name.text] = t;
        newDecls.push_back(std::move(d));
    }

    // `enum Tag { … };` / `struct Tag { … };` at file scope.
    void parseEnumDecl(std::vector<Comment> comments) {
        typeBodies = true;
        const bool isStruct = isIdent("struct");
        if (isStruct) parseStructType();
        else parseEnumType();
        typeBodies = false;
        TypeDecl& d = newDecls.back();
        d.comments.insert(d.comments.begin(), comments.begin(), comments.end());
        if (!isPunct(";"))
            failCode("E106", file, peek().loc, std::string("a global variable of the ") + (isStruct ? "struct" : "enum") + " type",
                     "mutable globals are ⛔ (main-spec §7.3); Lean definitions are constants.",
                     "pass the value as a parameter, or define the type alone: `" +
                         std::string(isStruct ? "struct S" : "enum E") + " { … };`.");
        ++i;
        for (Comment& c : takeComments(i, false)) d.comments.push_back(std::move(c));
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
        checkJumps(f.body, false, false);
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

    Parser p{toks, comments, definedNames, definedValues, fileName, 0, 0, {}, {}, {}, false, {}, {}};
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
        if (p.peek().kind == Token::Kind::Ident && classifyLibName(p.peek().text) == LibName::Output &&
            p.peek2().kind == Token::Kind::Punct && p.peek2().text == "(") {  // script mode (§8.2)
            Stmt st = p.parsePrint();
            st.script = true;
            st.comments = std::move(pending);
            pending.clear();
            for (Comment& c : p.takeComments(p.i, false)) st.comments.push_back(std::move(c));
            unit.scripts.push_back(std::move(st));
            continue;
        }
        const bool labelled = p.peek().kind == Token::Kind::Ident && p.isPunctAt(p.i + 1, ":") &&
                              p.toks[p.i + 2].kind == Token::Kind::Ident && p.toks[p.i + 2].text == "VERIFY_FORALL";
        if (labelled || (p.isIdent("VERIFY_FORALL") && p.isPunctAt(p.i + 1, "("))) {  // main-spec §9.3
            Verify v;
            if (labelled) {  // `name: VERIFY_FORALL(…)` — the label names the theorem (§9.4)
                Token n = p.toks[p.i];
                p.checkName(n);
                v.name = n.text;
                v.named = true;
                p.i += 2;
            }
            v.loc = p.peek().loc;
            v.line = v.loc.line;
            v.comments = std::move(pending);
            pending.clear();
            ++p.i;  // VERIFY_FORALL
            p.expectPunct("(", "after `VERIFY_FORALL`");
            while (true) {
                Type t = p.parseCType();
                Token n = p.expectName("the name of a `VERIFY_FORALL` variable");
                p.checkName(n);
                v.binders.emplace_back(t, n.text);
                if (!p.isPunct(",")) break;
                ++p.i;
            }
            p.expectPunct(")", "to close the `VERIFY_FORALL` variables");
            p.expectPunct("{", "to open the `VERIFY_FORALL` body");
            v.body = p.parseExpr();
            p.expectPunct(";", "after the `VERIFY_FORALL` expression");
            if (!p.isPunct("}"))
                p.failHere("a `VERIFY_FORALL` body holds one expression",
                           "the theorem states one proposition (main-spec §9.3).",
                           "join the checks with `&&`: `{ f(a) == g(a) && h(a) == a; }`.");
            ++p.i;
            for (Comment& c : p.takeComments(p.i, false)) v.comments.push_back(std::move(c));
            unit.verifies.push_back(std::move(v));
            continue;
        }
        if (p.isIdent("LEAN_DECL") && p.isPunctAt(p.i + 1, "(")) {  // raw Lean at file scope (§11.1)
            std::string text = p.parseRawLean("LEAN_DECL");
            if (p.isPunct(";")) ++p.i;
            std::string out;
            for (const Comment& c : pending) out += (c.block ? "/- " + c.text + " -/" : "-- " + c.text) + "\n";
            pending.clear();
            (unit.functions.empty() ? unit.leanDeclsBefore : unit.leanDeclsAfter).push_back(out + text);
            continue;
        }
        if (p.isIdent("typedef")) {  // main-spec §4.5
            p.parseTypedef(std::move(pending));
            pending.clear();
            for (TypeDecl& d : p.newDecls) unit.typeDecls.push_back(std::move(d));
            p.newDecls.clear();
            continue;
        }
        if ((p.isIdent("enum") || p.isIdent("struct")) && (p.isPunctAt(p.i + 1, "{") ||
                                  (p.toks[p.i + 1].kind == Token::Kind::Ident && p.isPunctAt(p.i + 2, "{")))) {
            p.parseEnumDecl(std::move(pending));  // `enum E { … };` / `struct P { … };` (main-spec §4.5)
            pending.clear();
            for (TypeDecl& d : p.newDecls) unit.typeDecls.push_back(std::move(d));
            p.newDecls.clear();
            continue;
        }
        bool isPartial = false, isInline = false, isStatic = false;
        Loc attrLoc = p.peek().loc;
        bool hasAttr = false;
        while (p.isPunct("[") && p.peek2().kind == Token::Kind::Punct && p.peek2().text == "[") {
            const std::string a = p.parseAttribute();
            isPartial = isPartial || a == "partial";
            isInline = isInline || a == "inline";
            hasAttr = true;
            for (Comment& c : p.takeComments(p.i, true)) pending.push_back(std::move(c));
        }
        Loc storageAt = p.peek().loc;
        while (p.isIdent("static") || p.isIdent("inline")) {  // main-spec §4.7, in either order
            (p.isIdent("static") ? isStatic : isInline) = true;
            ++p.i;
        }
        if (p.peek().kind != Token::Kind::Ident)
            p.failHere("expected a declaration, found " + describe(p.peek()), kSliceWhy,
                       "files contain functions; see spec/impl-spec.md.");
        int defLine = p.peek().loc.line;
        const bool isExtern = p.isIdent("extern");  // main-spec §7.2.1: Lean already defines it
        if (isExtern && (isStatic || isInline))
            fail(fileName, storageAt, "`static`/`inline` on an `extern` declaration",
                 "nothing is emitted for an `extern`.", "remove the qualifier.");
        if (isExtern) {
            if (hasAttr)
                fail(fileName, attrLoc, "an attribute on an `extern` declaration",
                     "nothing is emitted for it, so the attribute would be silently lost.", "remove it.");
            ++p.i;
        }
        Loc retAt = p.peek().loc;
        const size_t typeStart = p.i;
        Type retType = p.parseCType();
        const int stars = p.parseStars();
        Token name = p.expectName("a function name");
        p.checkName(name);
        if (!p.isPunct("(")) {  // a file-scope variable: a `const` one is a `def` (main-spec §7.3)
            bool isConst = false;
            for (size_t k = typeStart; k < p.i; ++k) isConst = isConst || p.toks[k].text == "const";
            if (isExtern) {  // `extern const T name;`: a value defined in Lean
                if (!isConst)
                    failCode("E106", fileName, name.loc, "`" + name.text + "` is a mutable `extern` variable",
                             "a Lean definition is a constant.", "declare it `extern const`.");
                p.i = typeStart;
                Type t = p.parseCType();
                std::vector<long long> dims;
                const int st = p.parseStars();
                p.expectName("the name of the `extern` constant");
                while (p.isPunct("[")) dims.push_back(p.parseArraySize());
                t = p.declare(t, st, dims.size(), retAt, false);
                if (p.isPunct("="))
                    p.failHere("an `extern` declaration with a value",
                               "`extern` means the value is defined in Lean (main-spec §7.2.1).",
                               "drop `extern` to define it here, or drop the value.");
                p.expectPunct(";", "after the `extern` declaration");
                Function g;
                g.name = name.text;
                g.defLine = defLine;
                g.isGlobal = true;
                g.isExtern = true;
                g.retType = t;
                pending.clear();
                unit.functions.push_back(std::move(g));
                continue;
            }
            if (hasAttr || isInline)
                fail(fileName, attrLoc, "an attribute or `inline` on the global `" + name.text + "`",
                     "they apply to functions.", "remove it.");
            if (!isConst)
                failCode("E106", fileName, name.loc, "`" + name.text + "` is a mutable global variable",
                         "Lean definitions are constants; a global that functions change would need "
                         "`StateM` or `IO.Ref`, which main-spec §7.3 keeps 🟡.",
                         "make it `const` if it never changes, or pass the state as a parameter.");
            p.i = typeStart;
            Stmt decl = p.parseDeclTail();  // `const T name = …;` / `const T a[N] = {…};`
            if (decl.noInit)
                fail(fileName, name.loc, "the constant `" + name.text + "` has no value",
                     "a `const` that is never set is always zero; that is almost always a mistake.",
                     "give it its value: `const " + cName(decl.type) + " " + name.text + " = …;`.");
            Function g;
            g.name = name.text;
            g.defLine = defLine;
            g.isGlobal = true;
            g.retType = decl.type;
            g.comments = std::move(pending);
            pending.clear();
            for (Comment& c : p.takeComments(p.i, false)) g.comments.push_back(std::move(c));
            g.body.push_back(std::move(decl));
            g.isPrivate = isStatic;  // `static const int N = …;` → `private def`
            unit.functions.push_back(std::move(g));
            continue;
        }
        retType = p.declare(retType, stars, 0, retAt, false);
        if (p.isPunct("(") && p.isPunctAt(p.i + 1, "*")) p.failFunctionPointer();
        p.expectPunct("(", "to open the parameter list of `" + name.text + "`");
        auto params = p.parseParams(name.text == "main");
        p.expectPunct(")", "to close the parameter list of `" + name.text + "`");
        if (!p.isPunct(";"))
            for (const auto& prm : params)
                if (prm.second.empty())
                    p.failHere("a parameter of `" + name.text + "` has no name",
                               "only a prototype may leave parameter names out; the body needs them.",
                               "name every parameter.");
        if (p.isPunct(";") && isExtern) {  // `extern T f(…);` — defined in Lean (§7.2.1)
            ++p.i;
            Function f;
            f.name = name.text;
            f.defLine = defLine;
            f.isExtern = true;
            f.retType = retType;
            f.params = std::move(params);
            pending.clear();
            unit.functions.push_back(std::move(f));
            continue;
        }
        if (p.isPunct(";")) {  // prototype: dropped (§7.1)
            if (hasAttr)
                fail(fileName, attrLoc, "an attribute on the prototype of `" + name.text + "`",
                     "prototypes are dropped, so the attribute would be silently lost.",
                     "put the attribute on the definition.");
            ++p.i;
            continue;
        }
        Function f = p.parseFunction(name.text, defLine, std::move(params), std::move(pending));
        if (isStatic && f.name == "main")
            fail(fileName, storageAt, "`static main`", "Lean runs a `main` it can see; a `private` one is not.",
                 "drop `static`.");
        f.isPartial = isPartial;
        f.isPrivate = isStatic;
        f.isInline = isInline;
        f.retType = retType;
        unit.functions.push_back(std::move(f));
        pending.clear();
    }
    unit.longDoubles = std::move(p.longDoubles);
    return unit;
}
