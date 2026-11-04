#include "lean.h"

// `typedef T name;` is `abbrev name := T` (main-spec §4.5); values keep the name
typedef bv(8) byte;
typedef unsigned int count_t;
typedef const char *text;
// a typedef of a typedef
typedef byte octet;

byte rotl1(byte x) { return (x << 1) | (x >> 7); }

count_t total(count_t a[], count_t n) {
    count_t s = 0;
    for (count_t i = 0; i < n; i++) s += a[i];
    return s;
}

text greet(bool formal) { return formal ? "Good day" : "hi"; }

// casts and mixed arithmetic see through the name (D19)
octet low(int v) {
    octet o = (octet)v;
    return o + BV(8, 1);
}
