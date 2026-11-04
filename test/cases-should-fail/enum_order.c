// an inductive has no order
enum Color { RED, GREEN };
bool f(enum Color a) { return a < GREEN; }
