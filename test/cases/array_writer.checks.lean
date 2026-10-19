-- writers return the modified array; call sites re-assign what they passed
example : fill #[0, 0] 5 = #[5, 6] := rfl
example : bump_at #[1, 2] 1 7 = #[1, 9] := rfl
example : zero_head #[4, 4] 0 = #[4, 4] := rfl
example : zero_head #[4, 4] 3 = #[0, 4] := rfl
example : set_either #[4, 4] 1 = #[1, 4] := rfl
example : set_either #[4, 4] 0 = #[4, 1] := rfl
example : set_last #[4, 4] 5 = #[5, 4] := rfl
example : set_last #[4, 4] 0 = #[4, 4] := rfl
example : clamp_low #[4, 4] (-3) = #[0, 4] := rfl
example : clamp_low #[4, 4] 20 = #[9, 4] := rfl
example : clamp_low #[4, 4] 5 = #[4, 4] := rfl
example : by_switch #[4, 4] 0 = #[1, 4] := rfl
example : by_switch #[4, 4] 7 = #[4, 2] := rfl
example : via_ptr #[9, 9, 9, 9] 8 = #[9, 9, 9, 8] := rfl
example : fill_row #[#[0, 0], #[0, 0]] 6 = #[#[6, 0], #[0, 6]] := rfl
example : wrap #[0, 0, 0] 7 = #[7, 8, 9] := rfl
example : outer #[0, 0, 0] 7 = #[7, 8, 9] := rfl
example : drive 7 = 23 := rfl
example : ptr_form 8 = 17 := rfl
example : rows 6 = 12 := rfl
