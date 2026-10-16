// main.cpp — command line entry: wire the layers together.
//
// Layer: driver (lex → parse → sort → emit). Input: `clean <input.c> [-o <output.lean>]` (argv).
// Output: one .lean file (default: the input path with `.c` → `.lean`), a
// one-line summary on stdout; diagnostics go to stderr with exit 1 (see
// diag.h), usage errors exit 2.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "diag.h"
#include "emitter.h"
#include "lexer.h"
#include "parser.h"
#include "toposort.h"

namespace {

void usage(const char* prog) {
    std::cerr << "usage: " << prog << " <input.c> [-o <output.lean>]\n";
}

std::string baseName(const std::string& path) {
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string defaultOutput(const std::string& in) {
    const std::string suffix = ".c";
    if (in.size() > suffix.size() &&
        in.compare(in.size() - suffix.size(), suffix.size(), suffix) == 0)
        return in.substr(0, in.size() - suffix.size()) + ".lean";
    return in + ".lean";
}

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::cerr << "clean: cannot read '" << path << "'\n";
        std::exit(2);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void writeFile(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        std::cerr << "clean: cannot write '" << path << "'\n";
        std::exit(2);
    }
    f << content;
}

}  // namespace

int main(int argc, char** argv) {
    std::string in, out;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o") {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            out = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
            return 0;
        } else if (!in.empty()) {
            usage(argv[0]);
            return 2;
        } else {
            in = arg;
        }
    }
    if (in.empty()) {
        usage(argv[0]);
        return 2;
    }
    if (out.empty()) out = defaultOutput(in);

    try {
        const std::string source = readFile(in);
        LexResult lexed = lex(source, baseName(in));
        TranslationUnit unit = parse(lexed.tokens, lexed.definedNames, baseName(in));
        sortDefinitions(unit);
        writeFile(out, emit(unit));
        std::cout << "clean: wrote " << out << " (" << unit.functions.size()
                  << " definition(s))\n";
        return 0;
    } catch (const DiagError& d) {
        std::cerr << d.render();
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "clean: internal error: " << e.what() << '\n';
        return 1;
    }
}
