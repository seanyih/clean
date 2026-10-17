// match patterns on UInt8 are unverified: switch only on int / unsigned for now
int f(uint8_t x) {
    switch (x) {
    case 1: return 1;
    default: return 0;
    }
}
