#include "lean.h"

// `char` → `Char`; character literals keep their spelling (main-spec §4.2).
// Lean's `Char` has no arithmetic, so numbers go through `unsigned` casts.
char first(char c) { return c; }

bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool isNewline(char c) { return c == '\n'; }

unsigned digitValue(char c) { return (unsigned)c - (unsigned)'0'; }

char fromCode(unsigned n) { return (char)n; }

char letterA(void) { return 65; }

char next(char c) { return (char)((unsigned)c + 1); }

int classify(char c) {
    if (c == 'a') return 1;
    if (c == ' ' || c == '\t') return 2;
    return 0;
}

char zero(void) { return '\0'; }

char quote(void) { return '\''; }

unsigned code(char c) { return (unsigned)c; }
