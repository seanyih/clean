#include <stdio.h>
#include <stdbool.h>

// `struct` is a Lean `structure`, derived so values print and compare (main-spec §4.5)
struct Point { int x; int y; };

// a typedef'd anonymous struct takes the typedef's name; fields of any type,
// several per line, arrays and nested structs
typedef struct {
    struct Point pos;
    int hist[3];
    double w, h;
    char name[16];
    bool on;
} Box;

// field reads, and a struct built from values: a complete positional list is `⟨…⟩`
struct Point add(struct Point a, struct Point b) {
    struct Point r = {a.x + b.x, a.y + b.y};
    return r;
}

// designated initialisers; missing fields are C's zero
Box make(int x) {
    Box b = {.pos = {x, 2 * x}, .w = 1.5, .name = "box"};
    return b;
}

// field writes are `{ p with … }` updates, nested ones too; an array field updates in place
Box moved(Box b, int dx) {
    b.pos.x += dx;
    b.hist[1] = b.pos.x;
    b.on = true;
    return b;
}

// a field of an array parameter's element is written: the caller's array changes, so the
// function returns it (D20)
void zeroFirst(struct Point ps[], int n) {
    if (n > 0) ps[0].x = 0;
}

// arrays of structs: initialised by nested lists, read and written element-wise
int sumX(void) {
    struct Point ps[3] = {{1, 2}, {3, 4}};
    ps[2].x = 10;
    zeroFirst(ps, 1);
    int s = 0;
    for (int i = 0; i < 3; i++) s += ps[i].x;
    return s;
}

// an uninitialised struct is all zeros, its array fields included
int zeros(void) {
    Box z;
    return z.hist[2] + z.pos.y;
}

// a field of a returned struct
int area(int x) { return make(x).pos.y + add(make(x).pos, make(1).pos).y; }

int main(void) {
    Box b = moved(make(3), 4);
    printf("%d %d %d %s\n", b.pos.x, b.pos.y, b.hist[1], b.name);
    printf("%d %d %d\n", sumX(), zeros(), area(2));
    return 0;
}
