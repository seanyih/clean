// two written array parameters (one directly, one through a call): clean
// returns one modified array per function, and a tuple is not built (D20)
void fill(int a[], int n) { a[0] = n; }
void bothways(int a[], int b[], int n) {
    fill(a, n);
    b[0] = n;
}
