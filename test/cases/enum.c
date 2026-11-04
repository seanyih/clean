#include <stdio.h>
#include <stdbool.h>

// `enum` is an `inductive` plus `open`, so enumerators are written bare (main-spec §4.5)
enum Op { ADD, SUB };

// an exhaustive switch needs no `default`; the C safety-net `return 0;` is dropped (§14.3)
int apply(enum Op op, int a, int b) {
    switch (op) {
    case ADD: return a + b;
    case SUB: return a - b;
    }
    return 0;
}

// written values: `def Level.toNat` gives each enumerator its C value
typedef enum { LOW = 1, MID = 5, HIGH } Level;

// grouped labels, a default, and a comparison
int weight(Level l) {
    int w = 0;
    switch (l) {
    case LOW:
    case MID:
        w = 1;
        break;
    default:
        w = 9;
        break;
    }
    if (l == HIGH) w = w + 100;
    return w;
}

// an enumerator is an `int` in C: conversions, arithmetic and printing use its value
int code(Level l) { return (int)l * 10; }
int next(Level l) { return l + 1; }
Level pick(bool hi) { return hi ? HIGH : LOW; }

// a switch on an integer may use enumerators as labels (their values)
Level fromInt(int n) {
    switch (n) {
    case LOW: return LOW;
    case MID: return MID;
    default: return HIGH;
    }
}

enum Dir { NORTH, EAST, SOUTH, WEST };
// a local of an enum type, uninitialised: the first enumerator
enum Dir turn(enum Dir d) {
    enum Dir r;
    switch (d) {
    case NORTH: r = EAST; break;
    case EAST: r = SOUTH; break;
    case SOUTH: r = WEST; break;
    case WEST: r = NORTH; break;
    }
    return r;
}

int main(void) {
    printf("%d %d %d\n", apply(ADD, 2, 3), weight(MID), code(HIGH));
    printf("%d %d\n", next(MID), (int)turn(WEST));
    return 0;
}
