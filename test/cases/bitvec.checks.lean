-- not yet run against Lean: `#guard` evaluates the translated functions.
#guard sbox_xor 0x0F#8 0xFF#8 = 0xF0#8
#guard mask 0xAB#8 = 0x0B#8
#guard nibble 0b0101#4 = 0b1010#4
#guard wide 0x0001#16 0x0001#16 = 0x0005#16
#guard bump 0xFF#8 = 0x00#8           -- wraps
#guard neg 1#8 = 0xFF#8
#guard isZero 0#8 = 1
#guard isZero 9#8 = 0
#guard lt 1#8 2#8 = 1
#guard lt 5#8 4#8 = 2
#guard lt 2#8 2#8 = 0
#guard fromNat 300 = 44#8
#guard toNat 200#8 = 200
#guard fromU8 7 = 7#8
