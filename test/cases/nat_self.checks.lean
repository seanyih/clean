-- well-founded recursion does not reduce by `rfl`, so these run the compiled
-- code. f counts down to its base case: f n = n.
#guard f 5 = 5
#guard f 0 = 0
#guard f 23 = 23
