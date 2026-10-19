#include "lean.h"

// `char *`, `const char *` and `char s[]` → `String` (main-spec §4.3);
// `const` is ignored (§4.7). Not built: the `<string.h>` helpers
// (`strlen`, `strcmp`, `strcat`, …).
char *greeting(void) { return "hello\n"; }

const char *title(void) { return "clean"; }

// indexing is `s.toList[i]!` (O(n), lint W011)
char first(char *s) { return s[0]; }
char third(char *s) {
    char *t = s;
    return t[2];
}

// `==` and `!=` compare the strings; a string used as a test means "non-empty" (§5.2)
int isHello(char *s) {
    if (s == "hello") return 1;
    return 0;
}
int nonEmpty(const char s[]) {
    if (s) return 1;
    return 0;
}
bool same(char *a, char *b) { return a == b; }

char *pick(bool c, char *a, char *b) {
    if (c) return a;
    return b;
}

// adjacent literals join; escapes are re-spelled for Lean
char *joined(void) { return "ab" "cd"; }
char *quoted(void) { return "say \"hi\"\t\x41"; }

// a local initialised from a literal
int viaLocal(void) {
    char s[] = "abc";
    char *p = "xyz";
    if (s != p) return 1;
    return 0;
}
