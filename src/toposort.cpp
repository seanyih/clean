// toposort.cpp — implementation of toposort.h.
//
// Tarjan's strongly-connected-components walk over the call graph. Roots are
// taken in source order and a function's callees in the order they appear in
// its body, so the result is deterministic and an already-ordered file comes
// out unchanged. Tarjan emits a component only after everything it calls,
// which is exactly the order Lean needs. A component with several functions,
// or one function that calls itself, is recursion.

#include "toposort.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace {

void collectCalls(const Expr& e, std::vector<std::string>& out) {
    if (e.kind == Expr::Kind::Call) out.push_back(e.text);
    for (const Expr& k : e.kids) collectCalls(k, out);
}

struct Tarjan {
    std::vector<std::vector<size_t>> callees;  // per function, in call order
    std::vector<int> index, low;
    std::vector<bool> onStack;
    std::vector<size_t> stack;
    int counter = 0;
    std::vector<std::vector<size_t>> components;  // callees first

    void visit(size_t v) {
        index[v] = low[v] = counter++;
        stack.push_back(v);
        onStack[v] = true;
        for (size_t w : callees[v]) {
            if (index[w] < 0) {
                visit(w);
                low[v] = std::min(low[v], low[w]);
            } else if (onStack[w]) {
                low[v] = std::min(low[v], index[w]);
            }
        }
        if (low[v] == index[v]) {
            std::vector<size_t> comp;
            size_t w;
            do {
                w = stack.back();
                stack.pop_back();
                onStack[w] = false;
                comp.push_back(w);
            } while (w != v);
            std::sort(comp.begin(), comp.end());  // members in source order
            components.push_back(std::move(comp));
        }
    }
};

}  // namespace

void sortDefinitions(TranslationUnit& unit) {
    const size_t n = unit.functions.size();
    std::map<std::string, size_t> index;
    for (size_t i = 0; i < n; ++i) index[unit.functions[i].name] = i;

    Tarjan t;
    t.callees.resize(n);
    t.index.assign(n, -1);
    t.low.assign(n, 0);
    t.onStack.assign(n, false);
    for (size_t i = 0; i < n; ++i) {
        std::vector<std::string> names;
        forEachExpr(unit.functions[i].body, [&](const Expr& e) { collectCalls(e, names); });
        for (const std::string& name : names)
            if (index.count(name)) t.callees[i].push_back(index.at(name));  // else a library call (getchar, fopen, …)
    }
    for (size_t i = 0; i < n; ++i)
        if (t.index[i] < 0) t.visit(i);

    std::vector<Function> sorted;
    sorted.reserve(n);
    int group = 0;
    for (const std::vector<size_t>& comp : t.components) {
        bool selfCall = comp.size() == 1 &&
            std::find(t.callees[comp[0]].begin(), t.callees[comp[0]].end(), comp[0]) !=
                t.callees[comp[0]].end();
        bool recursive = comp.size() > 1 || selfCall;
        if (comp.size() > 1) {
            bool anyPartial = false, allPartial = true;
            for (size_t i : comp) {
                anyPartial = anyPartial || unit.functions[i].isPartial;
                allPartial = allPartial && unit.functions[i].isPartial;
            }
            if (anyPartial && !allPartial) {
                std::string names;
                for (size_t i : comp) names += (names.empty() ? "" : ", ") + unit.functions[i].name;
                throw DiagError("E000", unit.fileName, Loc{unit.functions[comp[0]].defLine, 1},
                                "`[[lean::partial]]` on only some of the mutually recursive functions " + names,
                                "Lean requires `partial` on all or none of the functions in a `mutual` block.",
                                "put `[[lean::partial]]` on every one of them, or on none.");
            }
        }
        for (size_t i : comp) {
            Function& f = unit.functions[i];
            f.recursive = recursive;
            f.mutualGroup = comp.size() > 1 ? group : -1;
            sorted.push_back(std::move(f));
        }
        if (comp.size() > 1) ++group;
    }
    unit.functions = std::move(sorted);
}
