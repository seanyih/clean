enum Color { RED, GREEN };
enum Size { SMALL, BIG };
int f(enum Color c) { switch (c) { case SMALL: return 1; default: return 0; } }
