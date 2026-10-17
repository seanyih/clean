-- not yet run against Lean: `#guard` evaluates the translated functions.
#guard half 5.0 == 2.5
#guard scale 2.0 3.0 == 7.5
#guard poly 1.0 == -(3.0 - 2.5 + 1e-3)
#guard classify 0.0 = 0
#guard classify (-1.0) = 2
#guard classify 100.0 = 2
#guard classify 5.0 = 1
#guard near 1.0 1.25 = true
#guard near 1.0 2.0 = false
#guard eq 2.0 2.0 = true
#guard eq 2.0 3.0 = false
#guard truthy 0.0 = 0
#guard truthy 0.5 = 1
#guard wide 4.0 == 4.0
#guard fromInt (-3) == -3.0
#guard fromNat 3 == 3.0
#guard toByte 200.0 = 200
