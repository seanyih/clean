
-- building a string is not I/O: `scratch` stays a pure function
#check (scratch : Int → Int)
#guard scratch 4 = 5
#check (main : IO UInt32)
