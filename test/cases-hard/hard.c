/*
 * Hard end-to-end test for clean.
 *
 * Focus: recursion, mutual recursion, conditionals, Nat arithmetic,
 * and multiple recursive calls. No C runtime semantics are being tested.
 */

unsigned factorial(unsigned n) {
    if (n <= 1)
        return 1;
    return n * factorial(n - 1);
}

unsigned choose(unsigned n, unsigned k) {
    if (k == 0 || k == n)
        return 1;
    if (k > n)
        return 0;
    return choose(n - 1, k - 1) + choose(n - 1, k);
}

unsigned digit_sum(unsigned n) {
    if (n < 10)
        return n;
    return (n % 10) + digit_sum(n / 10);
}

unsigned reverse_digits(unsigned n, unsigned acc) {
    if (n == 0)
        return acc;
    return reverse_digits(n / 10, acc * 10 + n % 10);
}

unsigned digit_count(unsigned n) {
    if (n < 10)
        return 1;
    return 1 + digit_count(n / 10);
}

unsigned is_even(unsigned n) {
    if (n == 0)
        return 1;
    return is_odd(n - 1);
}

unsigned is_odd(unsigned n) {
    if (n == 0)
        return 0;
    return is_even(n - 1);
}
