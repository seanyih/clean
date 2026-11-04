// C has no == on structs either
struct P { int x; };
bool same(struct P a, struct P b) { return a == b; }
