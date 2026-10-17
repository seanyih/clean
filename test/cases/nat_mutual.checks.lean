-- well-founded recursion does not reduce by `rfl`, so these run the compiled
-- code. ping and pong count down to their base case: ping n = pong n = n.
#guard ping 4 = 4
#guard pong 4 = 4
#guard ping 0 = 0
#guard pong 0 = 0
#guard ping 10 = 10
#guard pong 17 = 17
