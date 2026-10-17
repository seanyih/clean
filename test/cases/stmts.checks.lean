-- every example must hold, or the translation of that statement is wrong

example : abs_val (-5) = 5 := by rfl
example : abs_val 4 = 4 := by rfl
example : sign 9 = 1 := by rfl
example : sign (-9) = -1 := by rfl
example : sign 0 = 0 := by rfl
example : sum_to_four 3 = 10 := by rfl
example : clamp 5 0 3 = 3 := by rfl
example : clamp (-2) 0 3 = 0 := by rfl
example : clamp 2 0 3 = 2 := by rfl
example : truthy 1 0 = 1 := by rfl
example : truthy 2 3 = 2 := by rfl
example : truthy 0 0 = 0 := by rfl
example : truthy 3 3 = 0 := by rfl
example : shadow 1 = 103 := by rfl
example : shadow 0 = 1 := by rfl
example : blocky 2 = 6 := by rfl
example : empty_branches 7 = 7 := by rfl
example : both_positive_doubled 4 = 8 := by rfl
example : both_positive_doubled (-4) = 0 := by rfl
example : one = 1 := by rfl
