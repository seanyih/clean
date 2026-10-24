// the text is a Lean String: an int cannot receive it
void f(void) { int n = 0; sprintf(n, "%d", 1); }
