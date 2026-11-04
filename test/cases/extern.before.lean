-- the Lean side of extern.c's `extern` declarations (main-spec §7.2.1)
def sbox_spec (x : BitVec 8) : BitVec 8 := x.rotateLeft 1 ^^^ 0x63#8
def gcd_spec (a b : Int) : Int := Int.ofNat (Int.gcd a b)
def LIMIT : Int := 1000
