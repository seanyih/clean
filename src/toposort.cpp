// toposort.cpp — implementation of toposort.h.
//
// Depth-first walk over the call graph. Roots are taken in source order and
// a function's callees are visited in the order they appear in its body, so
// the result is deterministic and a file that is already ordered comes out
// unchanged. Meeting a function that is still "open" on the walk means a
// cycle.

#include "toposort.h"

#include <map>
#include <string>
#include <vector>

namespace {

struct Call {
    std::string name;
    Loc loc;
};

void collectCalls(const Expr& e, std::vector<Call>& out) {
    if (e.kind == Expr::Kind::Call) out.push_back({e.text, e.loc});
    for (const Expr& k : e.kids) collectCalls(k, out);
}

struct Sorter {
    const TranslationUnit& unit;
    std::map<std::string, size_t> index;     // function name → position in source order
    std::vector<int> state;                  // 0 = unseen, 1 = open (on the walk), 2 = done
    std::vector<size_t> stack;               // open functions, outermost first
    std::vector<size_t> order;               // result: indices, callees first

    void visit(size_t f, const Loc* callSite) {
        if (state[f] == 2) return;
        if (state[f] == 1) reportCycle(f, callSite);
        state[f] = 1;
        stack.push_back(f);
        std::vector<Call> calls;
        collectCalls(unit.functions[f].body, calls);
        for (const Call& c : calls) visit(index.at(c.name), &c.loc);
        stack.pop_back();
        state[f] = 2;
        order.push_back(f);
    }

    [[noreturn]] void reportCycle(size_t again, const Loc* callSite) {
        size_t from = 0;
        while (stack[from] != again) ++from;
        std::string path;
        for (size_t k = from; k < stack.size(); ++k) path += unit.functions[stack[k]].name + " → ";
        path += unit.functions[again].name;
        throw DiagError("E000", unit.fileName, *callSite, "recursion: " + path,
                        "Lean needs a definition before its use; a cycle needs `mutual … end` "
                        "or a termination proof, which clean does not generate yet.",
                        "remove the cycle (e.g. make it a loop once loops are supported).");
    }
};

}  // namespace

void sortDefinitions(TranslationUnit& unit) {
    Sorter s{unit, {}, std::vector<int>(unit.functions.size(), 0), {}, {}};
    for (size_t i = 0; i < unit.functions.size(); ++i) s.index[unit.functions[i].name] = i;
    for (size_t i = 0; i < unit.functions.size(); ++i) s.visit(i, nullptr);

    std::vector<Function> sorted;
    sorted.reserve(unit.functions.size());
    for (size_t i : s.order) sorted.push_back(std::move(unit.functions[i]));
    unit.functions = std::move(sorted);
}
