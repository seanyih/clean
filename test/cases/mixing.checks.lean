-- evaluates the translated functions
#guard addMixed (-5) 3 = -2
#guard narrowMix 250 10 = 4
#guard narrowMix 5 (-1) = 4
#guard wideWins 200 100000 = 100200
#guard digit '7' = 7
#guard next 'a' = 'b'
#guard isA 'A' = true
#guard isA 'B' = false
#guard sameChar 'x' 'x' = true
#guard scaled (-2) == -3.0
#guard widen 1.5 2.0 == 3.5
#guard keepsWidth 1.5 == 3.0
#guard bvMix 250#8 10 = 4#8
#guard clampNeg (-3) = 0
#guard clampNeg 7 = 7
#guard shiftBy 1 4 = 16
#guard callMixed 300 = 44
#guard anyBits 0 = false
#guard anyBits 8 = true
