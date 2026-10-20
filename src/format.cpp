// format.cpp — implementation of format.h.
//
// One left-to-right walk. A backslash escape is one unit (`\n`, `\\`, `\x07`)
// and is copied into the literal text untouched; that is what makes "ends in a
// newline" exact (`\\n` is a backslash followed by `n`, not a newline). A `%`
// starts a conversion: flags, width, `.precision`, length letters, then the
// conversion letter. Flags, width and precision are accepted and ignored
// (reported as W006 by lint.cpp); everything C has beyond the table in
// main-spec §8.1 is refused with a reason.

#include "format.h"

namespace {

FormatResult refuse(const std::string& problem, const std::string& why, const std::string& fix) {
    FormatResult r;
    r.problem = problem;
    r.why = why;
    r.fix = fix;
    return r;
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

}  // namespace

FormatResult parseFormat(const std::string& body) {
    FormatResult r;
    std::string lit;           // literal text collected since the last conversion
    bool lastIsNewline = false;  // the last unit added to `lit` was the escape `\n`

    auto flushLit = [&]() {
        if (lit.empty()) return;
        FmtPiece p;
        p.lit = lit;
        r.pieces.push_back(std::move(p));
        lit.clear();
    };

    size_t i = 0;
    while (i < body.size()) {
        const char c = body[i];
        if (c == '\\') {  // an escape: `\n`, or `\xHH` (four characters)
            const size_t len = (i + 1 < body.size() && body[i + 1] == 'x') ? 4 : 2;
            const std::string unit = body.substr(i, len);
            lit += unit;
            lastIsNewline = unit == "\\n";
            i += len;
            continue;
        }
        if (c != '%') {
            lit += c;
            lastIsNewline = false;
            ++i;
            continue;
        }
        ++i;  // `%`
        if (i < body.size() && body[i] == '%') {
            lit += '%';
            lastIsNewline = false;
            ++i;
            continue;
        }
        FmtPiece p;
        while (i < body.size() && std::string("-+ #0").find(body[i]) != std::string::npos) {
            p.padded = true;
            ++i;
        }
        if (i < body.size() && body[i] == '*')
            return refuse("`*` as a width or precision in a `printf` format",
                          "the width would come from an argument, and clean ignores widths anyway.",
                          "write the width as a number, or drop it.");
        while (i < body.size() && isDigit(body[i])) {
            p.padded = true;
            ++i;
        }
        if (i < body.size() && body[i] == '.') {
            p.padded = true;
            ++i;
            if (i < body.size() && body[i] == '*')
                return refuse("`*` as a width or precision in a `printf` format",
                              "the precision would come from an argument, and clean ignores precisions anyway.",
                              "write the precision as a number, or drop it.");
            while (i < body.size() && isDigit(body[i])) ++i;
        }
        while (i < body.size() && std::string("hlzjtL").find(body[i]) != std::string::npos) ++i;  // length: no effect on Lean values
        if (i >= body.size())
            return refuse("a `printf` format ends inside a conversion", "a `%` must be followed by a conversion letter.",
                          "write `%%` for a literal percent sign, or finish the conversion.");
        const char conv = body[i++];
        switch (conv) {
        case 'd': case 'i': p.conv = 'd'; break;
        case 'u': p.conv = 'u'; break;
        case 's': p.conv = 's'; break;
        case 'c': p.conv = 'c'; break;
        case 'f': case 'F': case 'g': case 'G': case 'e': case 'E': p.conv = 'f'; break;
        case 'x': p.conv = 'x'; break;
        case 'X': p.conv = 'X'; break;
        default:
            return refuse(std::string("the conversion `%") + conv + "` in a `printf` format",
                          "main-spec §8.1 translates only %d %i %u %s %c %f %g %e %x %X and %%.",
                          "use one of those (cast the value first if needed).");
        }
        flushLit();
        r.pieces.push_back(std::move(p));
        lastIsNewline = false;
    }

    if (lastIsNewline) {  // cut the final `\n` off the last literal
        lit.erase(lit.size() - 2);
        r.newline = true;
    }
    flushLit();
    r.ok = true;
    return r;
}
