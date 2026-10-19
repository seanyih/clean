// a non-`void` function cannot write its array parameter: the modified array
// would have to come back as a second return value (OD11 c → D20)
int f(int a[]) { a[0] = 1; return 0; }
