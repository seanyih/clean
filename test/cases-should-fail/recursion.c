// a cycle needs `mutual … end` or a termination proof: not built yet
int ping(int n) { return pong(n - 1); }
int pong(int n) { return ping(n - 1); }
