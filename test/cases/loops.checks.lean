-- loops evaluate with the compiler (`#guard`); the kernel cannot reduce
-- the do-notation sugar, so `rfl`/`decide` do not apply here
#guard sum_range 5 = 10
#guard sum_digits = 45
#guard sum_odd_steps 10 = 22
#guard sum_u32 4 = 10
#guard sum_to 5 = 15
#guard every_other 10 = 5
#guard over_outer_var 3 = 9
#guard first_over 10 4 = 2
#guard find_index #[5, 6, 7] 3 7 = 2
#guard find_index #[5] 1 9 = -1
#guard count_down 10 = 9
#guard until_pair 3 = 5
#guard spin 5 = 5
#guard cap 7 = 8
#guard pairs_lt 4 = 6
#guard classify 5 = 34
#guard skip_threes 7 = 12
#guard fill_id #[9, 9, 9] 3 = #[0, 2, 4]
#guard commented 3 = 0
#check (print_row : Nat → IO Unit)

/-- info: i=0
i=1
done
done -/
#guard_msgs (whitespace := lax) in
#eval print_row 2

/-- info: ask
---
info: 5 -/
#guard_msgs (whitespace := lax) in
#eval ask_loop 5
