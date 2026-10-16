// lexer.cpp — implementation of lexer.h.
//
// Slices the input into tokens; whitespace and preprocessor lines vanish,
// comments become Comment tokens, everything unsupported still becomes a
// token (kind Other) so the parser can point at a line and column instead of
// the lexer guessing intent.

#include "lexer.h"

namespace {

bool isIdentStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool isIdentChar(char c) { return isIdentStart(c) || (c >= '0' && c <= '9'); }

bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool isHexDigit(char c) {
    return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

std::string trimBlank(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
    return s.substr(b, e - b);
}

const char* kSliceWhy =
    "clean is at slice 0: input files may contain # lines, comments and `int` "
    "functions with a single `return` statement (see spec/impl-spec.md).";

struct Lexer {
    const std::string& src;
    const std::string& file;
    size_t p = 0;
    int line = 1;
    int col = 1;

    bool atEnd() const { return p >= src.size(); }
    char cur() const { return atEnd() ? '\0' : src[p]; }
    char peekAt(size_t k) const { return p + k < src.size() ? src[p + k] : '\0'; }
    void step() {
        if (atEnd()) return;
        if (src[p] == '\n') {
            ++line;
            col = 1;
        } else {
            ++col;
        }
        ++p;
    }

    [[noreturn]] void fail(int l, int c, const std::string& what, const std::string& fix) {
        throw DiagError("E000", file, Loc{l, c}, what, kSliceWhy, fix);
    }

    // A `#…` line, including `\`-continuations. `#define NAME …` records NAME.
    void directive(LexResult& res) {
        step();  // '#'
        while (cur() == ' ' || cur() == '\t') step();
        std::string word;
        while (isIdentChar(cur())) {
            word += cur();
            step();
        }
        if (word == "define") {
            while (cur() == ' ' || cur() == '\t') step();
            std::string name;
            while (isIdentChar(cur())) {
                name += cur();
                step();
            }
            if (!name.empty()) res.definedNames.insert(name);
        }
        while (!atEnd() && cur() != '\n') {  // rest of the line, honouring `\` continuations
            if (cur() == '\\' && peekAt(1) == '\n') {
                step();
                step();
            } else {
                step();
            }
        }
        if (!atEnd()) step();  // the newline itself
    }

    Token lineComment() {
        Token t;
        t.kind = Token::Kind::Comment;
        t.loc = {line, col};
        step();
        step();  // '//'
        std::string text;
        while (!atEnd() && cur() != '\n') {
            text += cur();
            step();
        }
        t.text = trimBlank(text);
        return t;
    }

    Token blockComment() {
        Token t;
        t.kind = Token::Kind::Comment;
        t.loc = {line, col};
        step();
        step();  // '/*'
        std::string text;
        while (true) {
            if (atEnd())
                fail(t.loc.line, t.loc.col, "unterminated /* comment", "close it with `*/`.");
            if (cur() == '*' && peekAt(1) == '/') break;
            text += cur();
            step();
        }
        step();
        step();
        t.text = trimBlank(text);
        return t;
    }

    Token number() {
        Token t;
        t.kind = Token::Kind::IntLit;
        t.loc = {line, col};
        if (cur() == '0' && (peekAt(1) == 'x' || peekAt(1) == 'X')) {  // hex keeps its spelling in Lean
            t.text += cur();
            step();
            t.text += cur();
            step();
            if (!isHexDigit(cur()))
                fail(line, col, "hex literal has no digits", "write e.g. `0x1F`.");
            while (isHexDigit(cur())) {
                t.text += cur();
                step();
            }
        } else {
            while (isDigit(cur())) {
                t.text += cur();
                step();
            }
        }
        if (isIdentChar(cur()))
            fail(line, col, "integer suffixes are not translated",
                 "write a plain literal (`8`, `0xFF`, …).");
        return t;
    }

    Token stringLit() {
        Token t;
        t.kind = Token::Kind::Other;
        t.text = "string literal";
        t.loc = {line, col};
        step();  // '"'
        while (true) {
            if (atEnd() || cur() == '\n')
                fail(t.loc.line, t.loc.col, "unterminated string literal", "close it with `\"`.");
            if (cur() == '"') {
                step();
                break;
            }
            if (cur() == '\\' && peekAt(1) != '\0') step();  // skip escaped char
            step();
        }
        return t;
    }

    Token charLit() {
        Token t;
        t.kind = Token::Kind::Other;
        t.text = "character literal";
        t.loc = {line, col};
        step();  // '\''
        while (true) {
            if (atEnd() || cur() == '\n')
                fail(t.loc.line, t.loc.col, "unterminated character literal",
                     "close it with `'`.");
            if (cur() == '\'') {
                step();
                break;
            }
            if (cur() == '\\' && peekAt(1) != '\0') step();
            step();
        }
        return t;
    }

    Token ident() {
        Token t;
        t.kind = Token::Kind::Ident;
        t.loc = {line, col};
        while (isIdentChar(cur())) {
            t.text += cur();
            step();
        }
        return t;
    }
};

}  // namespace

LexResult lex(const std::string& source, const std::string& fileName) {
    LexResult res;
    Lexer L{source, fileName};
    static const std::string kPunct = "(){},;+-*/%";

    while (!L.atEnd()) {
        char c = L.cur();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            L.step();
            continue;
        }
        if (c == '#') {
            L.directive(res);
            continue;
        }
        if (c == '/' && L.peekAt(1) == '/') {
            res.tokens.push_back(L.lineComment());
            continue;
        }
        if (c == '/' && L.peekAt(1) == '*') {
            res.tokens.push_back(L.blockComment());
            continue;
        }
        if (c == '"') {
            res.tokens.push_back(L.stringLit());
            continue;
        }
        if (c == '\'') {
            res.tokens.push_back(L.charLit());
            continue;
        }
        if (isDigit(c)) {
            res.tokens.push_back(L.number());
            continue;
        }
        if (isIdentStart(c)) {
            res.tokens.push_back(L.ident());
            continue;
        }
        Token t;
        t.loc = {L.line, L.col};
        t.kind = kPunct.find(c) != std::string::npos ? Token::Kind::Punct : Token::Kind::Other;
        t.text = std::string(1, c);
        L.step();
        res.tokens.push_back(t);
    }

    Token end;
    end.kind = Token::Kind::End;
    end.loc = {L.line, L.col};
    res.tokens.push_back(end);
    return res;
}
