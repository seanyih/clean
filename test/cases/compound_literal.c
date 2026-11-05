// C99 compound literals `(T){ … }` (manual gap G6): a struct value built in
// place, positional or designated, as an argument, a return value, a constant.
#include <stdio.h>
#include <math.h>

typedef struct { double x; double y; } Point;

double distance(Point p1, Point p2) {
    return sqrt(pow(p2.x - p1.x, 2.0) + pow(p2.y - p1.y, 2.0));
}

Point addPoints(Point p1, Point p2) { return (Point){p1.x + p2.x, p1.y + p2.y}; }

Point zeroX(Point p) { return (Point){.x = 0, .y = p.y}; }

const Point origin = (Point){0.0, 0.0};

int main(void) {
    printf("%f\n", distance((Point){1.0, 2.0}, (Point){5.0, -1.0}));
    printf("%f\n", ((Point){1.5, 2.5}).x);
    printf("%f\n", addPoints((Point){1.5, 32}, (Point){-8, 0.25}).y);
    printf("%f\n", zeroX((Point){4.5, 3.5}).y + origin.x);
    return 0;
}
