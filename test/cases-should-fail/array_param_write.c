// writing through an array parameter would be lost for the caller (open decision OD11)
int f(int a[]) { a[0] = 1; return 0; }
