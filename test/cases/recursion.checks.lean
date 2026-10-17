-- `partial def` bodies are opaque to `rfl`, so these run the compiled code.
#guard fact 5 == 120
#guard fact 1 == 1
#guard is_even 10 == 1
#guard is_even 7 == 0
#guard is_odd 7 == 1
#guard parity_sum 7 == 1
#guard parity_sum 4 == 10
#guard not_recursive 3 == 7
