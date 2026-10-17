-- not yet run against Lean: `#guard` evaluates the translated functions.
#guard first 'x' = 'x'
#guard isDigit '7' = true
#guard isDigit 'a' = false
#guard isNewline '\n' = true
#guard digitValue '7' = 7
#guard fromCode 66 = 'B'
#guard letterA = 'A'
#guard next 'a' = 'b'
#guard classify 'a' = 1
#guard classify '\t' = 2
#guard classify 'z' = 0
#guard zero = Char.ofNat 0
#guard quote = '\''
#guard code 'A' = 65
