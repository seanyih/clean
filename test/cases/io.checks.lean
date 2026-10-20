
-- the types: I/O is `IO`, and it spreads to every caller
#check (greet : Int → IO Unit)
#check (report : Int → IO Int)
#check (total : Int → IO Int)
#check (fill : Array Int → Int → IO (Array Int))
#check (arrays : IO Unit)
#check (countdown : Nat → IO Unit)
#check (main : IO UInt32)
-- a function that prints nothing stays pure
#guard twice 4 = 8

-- the output (stdout only; `IO.eprint` goes to stderr)
/-- info: filled
9 -/
#guard_msgs (whitespace := lax) in
#eval arrays

/-- info: 3 2 1 0 -/
#guard_msgs (whitespace := lax) in
#eval countdown 3
