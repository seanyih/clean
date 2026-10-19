// int → unsigned is converted now (D19, lint W009); an array is not a number, whatever the target
unsigned f(int a[]) { return a; }
