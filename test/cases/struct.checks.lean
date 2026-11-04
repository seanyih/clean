#guard add ⟨1, 2⟩ ⟨3, 4⟩ == ⟨4, 6⟩
#guard (make 2).pos == ⟨2, 4⟩
#guard (make 2).hist == #[0, 0, 0]
#guard (make 2).name == "box"
#guard (moved (make 3) 4).pos.x == 7
#guard (moved (make 3) 4).hist == #[0, 7, 0]
#guard (moved (make 3) 4).on
#guard zeroFirst #[⟨5, 5⟩, ⟨6, 6⟩] 2 == #[⟨0, 5⟩, ⟨6, 6⟩]
#guard sumX = 13
#guard zeros = 0
#guard area 2 = 10
-- the derived instances: printing and comparing values in checks
#guard (default : Point) == ⟨0, 0⟩
#eval make 1
