-- arith.checks.lean — appended to the generated output by test/run_e2e.sh,
-- then the whole file is type-checked with `lean`. If the translation is
-- right, every `example` proves by `rfl` and `lean` exits 0.

example : add 2 3 = 5 := by rfl
example : twice 4 = 8 := by rfl
example : triple 3 = 9 := by rfl
example : poly 10 4 = 15 := by rfl
example : neg 0 = -1 := by rfl
example : mask 1 = 256 := by rfl
example : zero = 0 := by rfl
example : bits 1 = 11 := by rfl
