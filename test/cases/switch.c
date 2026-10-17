#include "lean.h"

// grouped labels become an or-pattern; `default` may sit in the middle
int classify(int op) {
    int r = 0;
    switch (op) {
    case 0:
    case 1:
        r = 10;
        break;
    default:
        r = 99;
        break;
    case 2:
        r = 20;
        break;
    }
    return r;
}

// every case returns (no `break` needed); negative, hex and binary labels
int name_of(int x) {
    switch (x) {
    case -1: return 100;
    case 0x10: return 200;
    case 0b11: return 300;
    default: return 0;
    }
}

// no `default`: nothing happens; braced case bodies; `if` inside a case
int bump(int x, int k) {
    int r = x;
    switch (k) {
    case 1: {
        if (x > 5) r = r - 1;
        r = r + 100;
        break;
    }
    case 2:
        r = r * 2;
        break;
    }
    return r;
}

// a switch in the middle of a function, scrutinee is an expression
int shifted(int a, int b) {
    int r = 0;
    switch (a + b) {
    case 3:
        r = 1;
    }
    return r + a;
}
