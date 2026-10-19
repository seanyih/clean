// the write escapes through the call: `leak` returns `int`, so it cannot
// also hand the modified array back (OD11 c → D20)
void fill(int a[], int n) { a[0] = n; }
int leak(int a[], int n) {
    fill(a, n);
    return 0;
}
