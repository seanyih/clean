// a writer's modified array can only be written back into a whole variable,
// not into `m[1]` (D20)
void fill(int a[], int n) { a[0] = n; }
void use(int m[][2]) {
    fill(m[1], 1);
}
