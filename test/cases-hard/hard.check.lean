/-!
Hard semantic checks for the C-to-Lean translation.

The generated Lean file is concatenated before this file by
run_e2e_hard.sh. These examples must be provable by Lean's kernel.
-/

-- Single recursive call and arithmetic.
example : factorial 0 = 1 := by decide
example : factorial 1 = 1 := by decide
example : factorial 8 = 40320 := by decide

-- Two recursive branches, plus the out-of-range branch.
example : choose 8 0 = 1 := by decide
example : choose 8 8 = 1 := by decide
example : choose 8 3 = 56 := by decide
example : choose 12 5 = 792 := by decide
example : choose 5 7 = 0 := by decide

-- Recursive decimal decomposition.
example : digit_sum 0 = 0 := by decide
example : digit_sum 7 = 7 := by decide
example : digit_sum 123456 = 21 := by decide
example : digit_sum 999999 = 54 := by decide

-- Tail-recursive accumulator transformation.
example : reverse_digits 0 0 = 0 := by decide
example : reverse_digits 1234 0 = 4321 := by decide
example : reverse_digits 1200 0 = 21 := by decide

-- Recursive depth computed from decimal digits.
example : digit_count 0 = 1 := by decide
example : digit_count 9 = 1 := by decide
example : digit_count 100000 = 6 := by decide

-- Mutual recursion.
example : is_even 0 = 1 := by decide
example : is_odd 0 = 0 := by decide
example : is_even 100 = 1 := by decide
example : is_odd 101 = 1 := by decide
example : is_even 101 = 0 := by decide
