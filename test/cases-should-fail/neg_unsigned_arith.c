// a negative literal where an unsigned value is needed, nested in
// arithmetic — the direct `x = -1;` form is refused too (neg_literal_unsigned)
unsigned f(void) {
    unsigned x = 0;
    x += -2;
    return x;
}
