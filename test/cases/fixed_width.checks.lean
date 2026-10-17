-- not yet run against Lean (no toolchain where this was written): `#guard`
-- evaluates the translated functions, so a wrong translation fails here.
#guard add8 200 100 = 44            -- UInt8 wraps
#guard mix32 5 2 = 20
#guard byte 7 = 7
#guard pack 0x12 0x34 = 0x1234
#guard shifty 1 1 = 4
#guard flags 0xF0 0x3C = 0xFFFFFF3F
#guard widen 200 = 200
#guard narrow 300 = 44
#guard signedOf (Int8.ofInt (-5)) = -5
#guard wrap8 200 = Int8.ofInt (-56)
#guard fromInt (-1) = 255
#guard cmp8 1 2 = 1
#guard cmp8 2 2 = 0
#guard cmp8 3 2 = 2
#guard truth8 0 = 0
#guard truth8 5 = 1
