
-- the types: `exit` is I/O, and it spreads to every caller
#check (half : Int → IO Int)
#check (positive : Int → IO Int)
#check (stop : Int → IO Unit)
#check (main : IO UInt32)
