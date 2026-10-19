// D19 converts mixed integer types, but a bool is not a number: main-spec §5.3 has no row
uint8_t f(uint8_t a, bool b) { return a + b; }
