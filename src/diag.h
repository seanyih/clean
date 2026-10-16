// diag.h — how `clean` reports a refusal (main-spec §13.1, decision D5).
//
// Layer: shared utility. No IO of its own: the layers throw DiagError with a
// location and a short why/fix pair; main catches it, renders it to stderr
// and exits 1. Nothing is ever emitted after an error — the tool rejects
// instead of guessing.
//
// E000 is the placeholder code for "not translated in the current slice".
// The catalogue codes of the main spec (E101, W001, …) are introduced
// together with the features that need them.

#pragma once

#include <stdexcept>
#include <string>
#include <utility>

struct Loc {
    int line = 0;  // 1-based
    int col = 0;   // 1-based
};

class DiagError : public std::runtime_error {
public:
    DiagError(std::string code, std::string file, Loc loc, std::string what,
              std::string why, std::string fix)
        : std::runtime_error(what), code_(std::move(code)), file_(std::move(file)),
          loc_(loc), why_(std::move(why)), fix_(std::move(fix)) {}

    // §13.1:
    //   E000  file.c:LINE:COL  <what>
    //         why:  <one sentence>
    //         fix:  <concrete rewrite>
    std::string render() const {
        std::string s;
        s += code_;
        s += "  ";
        s += file_;
        s += ":" + std::to_string(loc_.line);
        s += ":" + std::to_string(loc_.col);
        s += "  ";
        s += what();
        s += "\n      why:  " + why_;
        s += "\n      fix:  " + fix_;
        s += "\n";
        return s;
    }

private:
    std::string code_;
    std::string file_;
    Loc loc_;
    std::string why_;
    std::string fix_;
};
