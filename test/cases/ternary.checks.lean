example : max2 3 7 = 7 := by rfl
example : max2 (-2) (-5) = -2 := by rfl
example : sign 9 = 1 := by rfl
example : sign (-4) = -1 := by rfl
example : sign 0 = 0 := by rfl
example : pick true (-3) 8 = -3 := by rfl
example : pick false (-3) 8 = 8 := by rfl
#guard clampAdd 20 1 = 12
#guard clampAdd 0 (-3) = 4
#guard clampAdd 0 0 = 0
#guard between 1 5 9
#guard !between 1 0 9
#guard !between 1 10 9
#guard word true = "yes"
#guard word false = "no"
#guard sat8 200 100 = 255
#guard sat8 20 100 = 120
#guard absd (-2.5) == 2.5
