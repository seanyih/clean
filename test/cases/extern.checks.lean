#guard sbox_twice 0#8 = sbox_spec 0x63#8
#guard bounded_gcd 12 18 = 6
#guard bounded_gcd 2000 4 = 0
example : ∀ x y : BitVec 8, agrees x y = true := by
  intro x y; simp [agrees, BitVec.xor_comm]
