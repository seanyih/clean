// lexer.cpp — implementation of lexer.h.
//
// Slices the input into tokens; whitespace and preprocessor lines vanish,
// comments become Comment tokens, everything unsupported still becomes a
// token (kind Other) so the parser can point at a line and column instead of
// the lexer guessing intent.

#include "lexer.h"

#include <cctype>

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
    "clean translates scalar functions made of locals, assignments, `if`, "
    "`switch` and `return` (see spec/impl-spec.md).";

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

    // A `#…` line, including `\`-continuations. `#define NAME …` records NAME;
    // `#define NAME <integer literal>` also records the value (for `bv(NAME)`).
    void directive(LexResult& res) {
        step();  // '#'
        while (cur() == ' ' || cur() == '\t') step();
        std::string word;
        while (isIdentChar(cur())) {
            word += cur();
            step();
        }
        std::string rest;  // the rest of the line, continuations joined
        while (!atEnd() && cur() != '\n') {
            if (cur() == '\\' && peekAt(1) == '\n') {
                step();
                step();
                rest += ' ';
            } else {
                rest += cur();
                step();
            }
        }
        if (!atEnd()) step();  // the newline itself
        if (word == "pragma") {  // `#pragma lean NAME`
            size_t k = 0;
            while (k < rest.size() && (rest[k] == ' ' || rest[k] == '\t')) ++k;
            if (rest.compare(k, 4, "lean") == 0) {
                k += 4;
                while (k < rest.size() && (rest[k] == ' ' || rest[k] == '\t')) ++k;
                size_t e = k;
                while (e < rest.size() && isIdentChar(rest[e])) ++e;
                if (e > k) res.pragmas.insert(rest.substr(k, e - k));
            }
            return;
        }
        if (word != "define") return;
        size_t k = 0;
        while (k < rest.size() && (rest[k] == ' ' || rest[k] == '\t')) ++k;
        size_t nameStart = k;
        while (k < rest.size() && isIdentChar(rest[k])) ++k;
        if (k == nameStart) return;
        const std::string name = rest.substr(nameStart, k - nameStart);
        res.definedNames.insert(name);
        if (k < rest.size() && rest[k] == '(') return;  // function-like macro: no value
        size_t cut = rest.find("//", k);
        std::string body = trimBlank(rest.substr(k, cut == std::string::npos ? std::string::npos : cut - k));
        bool lit = !body.empty();
        if (lit && body.size() > 2 && body[0] == '0' && (body[1] == 'x' || body[1] == 'X')) {
            for (size_t i = 2; i < body.size(); ++i) lit = lit && isHexDigit(body[i]);
        } else {
            for (char c : body) lit = lit && isDigit(c);
        }
        if (lit) res.definedValues[name] = body;
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
        t.block = true;
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
        } else if (cur() == '0' && (peekAt(1) == 'b' || peekAt(1) == 'B')) {
            t.text += '0';  // Lean spells it `0b…`; normalise the prefix to lower case
            step();
            t.text += 'b';
            step();
            if (cur() != '0' && cur() != '1')
                fail(line, col, "binary literal has no digits", "write e.g. `0b1010`.");
            while (cur() == '0' || cur() == '1') {
                t.text += cur();
                step();
            }
            if (isDigit(cur()))
                fail(line, col, "binary literal contains a digit other than 0 or 1",
                     "fix the literal.");
        } else {
            std::string intPart, frac, exp;
            bool dot = false;
            while (isDigit(cur())) {
                intPart += cur();
                step();
            }
            if (cur() == '.') {  // `1.5`, `1.`, `.5`
                dot = true;
                step();
                while (isDigit(cur())) {
                    frac += cur();
                    step();
                }
            }
            if ((cur() == 'e' || cur() == 'E') &&
                (isDigit(peekAt(1)) || ((peekAt(1) == '+' || peekAt(1) == '-') && isDigit(peekAt(2))))) {
                exp = "e";
                step();
                if (cur() == '+' || cur() == '-') {
                    if (cur() == '-') exp += '-';
                    step();
                }
                while (isDigit(cur())) {
                    exp += cur();
                    step();
                }
            }
            if (dot || !exp.empty()) {  // a floating literal, in Lean's spelling
                t.kind = Token::Kind::FloatLit;
                t.text = (intPart.empty() ? "0" : intPart);
                if (dot) t.text += "." + (frac.empty() ? "0" : frac);
                t.text += exp;
                if (cur() == 'f' || cur() == 'F' || cur() == 'l' || cur() == 'L') step();  // `1.5f`
                if (isIdentChar(cur()))
                    fail(line, col, "unsupported suffix on a floating literal", "write a plain literal.");
                return t;
            }
            t.text = intPart;
        }
        if (isIdentChar(cur()))
            fail(line, col, "integer suffixes are not translated",
                 "write a plain literal (`8`, `0xFF`, …).");
        return t;
    }

    // The escape sequence after a `\` (already consumed), re-spelled the way Lean
    // writes it; shared by character and string literals.
    std::string escape(Loc at, const char* what) {
        char e = cur();
        step();
        switch (e) {
        case 'n': return "\\n";
        case 't': return "\\t";
        case 'r': return "\\r";
        case '\\': return "\\\\";
        case '\'': return "\\'";
        case '"': return "\\\"";
        case 'a': return "\\x07";
        case 'b': return "\\x08";
        case 'f': return "\\x0c";
        case 'v': return "\\x0b";
        default: break;
        }
        static const char* hex = "0123456789abcdef";
        if (e >= '0' && e <= '7') {  // octal: up to three digits
            int v = e - '0';
            for (int k = 0; k < 2 && cur() >= '0' && cur() <= '7'; ++k) {
                v = v * 8 + (cur() - '0');
                step();
            }
            if (v > 255) fail(at.line, at.col, std::string("octal escape out of range in a ") + what, "use at most `\\377`.");
            return std::string("\\x") + hex[(v >> 4) & 15] + hex[v & 15];
        }
        if (e == 'x' && isHexDigit(cur()) && isHexDigit(peekAt(1))) {
            std::string out = std::string("\\x") + (char)std::tolower(cur()) + (char)std::tolower(peekAt(1));
            step();
            step();
            return out;
        }
        fail(at.line, at.col, std::string("unsupported escape in a ") + what,
             "use `\\n`, `\\t`, `\\0`, `\\xHH` or a plain character.");
    }

    // `"abc"` → `"abc"`, escapes re-spelled for Lean.
    Token stringLit() {
        Token t;
        t.kind = Token::Kind::StringLit;
        t.loc = {line, col};
        step();  // '"'
        std::string body;
        while (true) {
            if (atEnd() || cur() == '\n')
                fail(t.loc.line, t.loc.col, "unterminated string literal", "close it with `\"`.");
            if (cur() == '"') {
                step();
                break;
            }
            if (cur() == '\\') {
                step();
                body += escape(t.loc, "string literal");
            } else {
                body += cur();  // plain characters, UTF-8 bytes included, pass through
                step();
            }
        }
        t.text = "\"" + body + "\"";
        return t;
    }

    // `'a'` → `'a'`; escapes are re-spelled the way Lean writes them.
    Token charLit() {
        Token t;
        t.kind = Token::Kind::CharLit;
        t.loc = {line, col};
        step();  // '\''
        std::string body;
        if (atEnd() || cur() == '\n')
            fail(t.loc.line, t.loc.col, "unterminated character literal", "close it with `'`.");
        if (cur() == '\\') {
            step();
            body = escape(t.loc, "character literal");
            if (body == "\\\"") body = "\"";  // `'\"'` is just `'"'`
        } else if (cur() == '\'') {
            fail(t.loc.line, t.loc.col, "empty character literal", "write a character between the quotes.");
        } else if ((unsigned char)cur() >= 0x80) {
            fail(t.loc.line, t.loc.col, "non-ASCII character literal is not translated",
                 "use an ASCII character or its number.");
        } else {
            body = std::string(1, cur());
            step();
        }
        if (cur() != '\'')
            fail(t.loc.line, t.loc.col, "multi-character literals are not translated",
                 "a character literal holds exactly one character.");
        step();
        t.text = "'" + body + "'";
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
    static const std::string kPunct = "(){}[],;:+-*/%<>=!&|^~";
    static const char* const kTwoChar[] = {"<=", ">=", "==", "!=", "&&", "||", "++", "--", "+=",
                                           "-=", "*=", "/=", "%=", "<<", ">>", "&=", "|=", "^="};

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
        if (isDigit(c) || (c == '.' && isDigit(L.peekAt(1)))) {
            res.tokens.push_back(L.number());
            continue;
        }
        if (isIdentStart(c)) {
            res.tokens.push_back(L.ident());
            continue;
        }
        Token t;
        t.loc = {L.line, L.col};
        if ((c == '<' && L.peekAt(1) == '<' && L.peekAt(2) == '=') ||
            (c == '>' && L.peekAt(1) == '>' && L.peekAt(2) == '=')) {  // `<<=` `>>=`
            t.kind = Token::Kind::Punct;
            t.text = std::string{c, c, '='};
            L.step();
            L.step();
            L.step();
            res.tokens.push_back(t);
            continue;
        }
        bool two = false;
        for (const char* op : kTwoChar)
            if (c == op[0] && L.peekAt(1) == op[1]) two = true;
        if (two) {  // `<=`, `&&`, `++`, `+=` …
            t.kind = Token::Kind::Punct;
            t.text = std::string{c, L.peekAt(1)};
            L.step();
            L.step();
            res.tokens.push_back(t);
            continue;
        }
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
