example : rotl1 0x81#8 = 0x03#8 := by decide
#guard total #[1, 2, 3, 4] 3 = 6
#guard total #[1, 2, 3, 4] 4 = 10
#guard greet true = "Good day"
#guard greet false = "hi"
example : low 255 = 0#8 := by decide
example : low (-2) = 0xFF#8 := by decide
-- the abbreviations are the types they name
example : byte = BitVec 8 := rfl
example : octet = BitVec 8 := rfl
