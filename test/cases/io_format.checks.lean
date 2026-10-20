
#check (ints : Int → Nat → UInt8 → Int64 → Char → IO Unit)
#check (hex : Nat → UInt8 → BitVec 8 → Int → IO Unit)
#check (floats : Float → Float32 → IO Unit)
#check (text : String → Char → IO Unit)

-- the generated helper
#guard hexOf 255 = "ff"
#guard hexOf 0 = "0"
#guard hexOf 4096 = "1000"

/-- info: ff FF
ff AB
ff
ff -/
#guard_msgs (whitespace := lax) in
#eval hex 255 255 0xAB#8 255

/-- info: [5] [5] [ff] [1.500000] -/
#guard_msgs (whitespace := lax) in
#eval padded 5 255 1.5
