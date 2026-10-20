// C skips the right side of && when the left decides; Lean would run the IO call anyway
int loud(int x) { printf("loud\n"); return x; }
void f(int a) { if (a > 0 && loud(a) > 0) { return; } }
