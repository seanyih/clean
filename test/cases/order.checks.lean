-- order.checks.lean — the definitions only type-check if clean put every
-- callee above its caller; the `example`s then check the values.

example : add 2 3 = 5 := by rfl
example : twice 4 = 8 := by rfl
example : quad 3 = 12 := by rfl
example : five = 5 := by rfl
example : mix 5 = 10 := by rfl
