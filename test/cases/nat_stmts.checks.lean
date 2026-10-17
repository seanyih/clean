-- well-founded recursion does not reduce by `rfl`, so these run the compiled
-- code. digit_sum adds the decimal digits.
#guard digit_sum 0 = 0
#guard digit_sum 7 = 7
#guard digit_sum 1234 = 10
#guard digit_sum 907 = 16
