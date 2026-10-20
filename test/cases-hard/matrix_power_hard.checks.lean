/-!
Proof checks for recursive matrix exponentiation.
The runner concatenates the translated C source before this file.
-/

def fibBase : Array Int :=
  #[1, 1, 0, 0,
    1, 0, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1]

def fibPower0 : Array Int :=
  matrix_power fibBase 0 (Array.replicate 16 0)

def fibPower1 : Array Int :=
  matrix_power fibBase 1 (Array.replicate 16 0)

def fibPower10 : Array Int :=
  matrix_power fibBase 10 (Array.replicate 16 0)

def fibPower25 : Array Int :=
  matrix_power fibBase 25 (Array.replicate 16 0)

def fibPower50 : Array Int :=
  matrix_power fibBase 50 (Array.replicate 16 0)

-- Identity and copy cases.
example : fibPower0[0]! = 1 := by decide
example : fibPower0[5]! = 1 := by decide
example : fibPower0[1]! = 0 := by decide
example : fibPower1[0]! = 1 := by decide
example : fibPower1[1]! = 1 := by decide
example : fibPower1[4]! = 1 := by decide
example : fibPower1[5]! = 0 := by decide

-- Exponentiation by squaring and recursive matrix multiplication.
example : fibPower10[0]! = 89 := by decide
example : fibPower10[1]! = 55 := by decide
example : fibPower10[4]! = 55 := by decide
example : fibPower10[5]! = 34 := by decide
example : fibPower10[10]! = 1 := by decide
example : fibPower10[15]! = 1 := by decide

example : fibPower25[0]! = 121393 := by decide
example : fibPower25[1]! = 75025 := by decide
example : fibPower25[4]! = 75025 := by decide
example : fibPower25[5]! = 46368 := by decide

-- Larger exact integer calculations.
example : fibPower50[0]! = 20365011074 := by decide
example : fibPower50[1]! = 12586269025 := by decide
example : fibPower50[4]! = 12586269025 := by decide
example : fibPower50[5]! = 7778742049 := by decide

-- Further consumers of the computed matrix.
example : matrix_vector_row fibPower10 #[1, 0, 0, 0] 0 = 89 := by decide
example : matrix_vector_row fibPower10 #[1, 0, 0, 0] 1 = 55 := by decide
example : fibonacci_block_sum fibPower10 = 233 := by decide
example : weighted_checksum fibPower10 16 = 705 := by decide

-- Independent scalar recursion.
example : power_of_two 0 = 1 := by decide
example : power_of_two 10 = 1024 := by decide
example : power_of_two 30 = 1073741824 := by decide
example : factorial 0 = 1 := by decide
example : factorial 10 = 3628800 := by decide
