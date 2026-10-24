
-- the types: reading is `IO`, and it spreads to every caller
#check (sum_pair : IO Int)
#check (count : IO Nat)
#check (main : IO UInt32)

-- the float reader understands what C's `%lf` does for ordinary input
#guard parseFloat "1.5" == some 1.5
#guard parseFloat "-2e2" == some (-200.0)
#guard parseFloat ".5" == some 0.5
#guard parseFloat "12" == some 12.0
#guard parseFloat "abc" == none
#guard parseFloat "1.5x" == none
