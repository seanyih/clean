// an integer may hold no enumerator's value: clean refuses the conversion
enum Color { RED, GREEN };
enum Color f(int n) { return n; }
