-- every example must hold, or the `unsigned` → `Nat` translation is wrong

example : twice 21 = 42 := by rfl
example : mod_sum 7 3 = 23 := by rfl
example : mod_sum 3 7 = 7 := by rfl  -- `3 - 7` truncates to 0 on `Nat` (W002)
example : bump 9 = 10 := by rfl
