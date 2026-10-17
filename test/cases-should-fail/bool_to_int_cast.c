// a bool is not a number; no cast exists in main-spec §5.3
int f(bool b) { return (int)b; }
