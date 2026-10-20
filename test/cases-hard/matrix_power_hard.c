/*
 * Recursive matrix-exponentiation stress test for clean.
 *
 * No loops, structs, pointers, ternary expressions, or C runtime behavior.
 * Array writers write exactly one array parameter. Counters count down.
 *
 * The top-left 2x2 block is the Fibonacci transition matrix:
 *     [1 1]
 *     [1 0]
 * The remaining diagonal entries are 1, so this is a 4x4 matrix.
 */

int dot_product(int a[16], int b[16],
                unsigned row, unsigned col, unsigned remaining) {
    if (remaining == 0)
        return 0;

    unsigned k = 4 - remaining;
    return a[row * 4 + k] * b[k * 4 + col]
        + dot_product(a, b, row, col, remaining - 1);
}

/* Write all 16 entries of a*b into out. */
void matrix_multiply(int a[16], int b[16],
                     int out[16], unsigned remaining) {
    if (remaining == 0)
        return;

    unsigned idx = 16 - remaining;
    unsigned row = idx / 4;
    unsigned col = idx % 4;

    out[idx] = dot_product(a, b, row, col, 4);
    matrix_multiply(a, b, out, remaining - 1);
}

/* Copy src into out. */
void copy_matrix(int src[16], int out[16], unsigned remaining) {
    if (remaining == 0)
        return;

    unsigned idx = 16 - remaining;
    out[idx] = src[idx];
    copy_matrix(src, out, remaining - 1);
}

/* Construct the 4x4 identity matrix. */
void identity_matrix(int out[16], unsigned remaining) {
    if (remaining == 0)
        return;

    unsigned idx = 16 - remaining;
    out[idx] = 0;
    if (idx / 4 == idx % 4)
        out[idx] = 1;

    identity_matrix(out, remaining - 1);
}

/*
 * Exponentiation by squaring:
 * A^0 = I; A^1 = A
 * A^n = (A^(n/2))^2 for even n
 * A^n = (A^(n/2))^2 * A for odd n
 */
void matrix_power(int base[16], unsigned exp, int out[16]) {
    if (exp == 0) {
        identity_matrix(out, 16);
        return;
    }

    if (exp == 1) {
        copy_matrix(base, out, 16);
        return;
    }

    int half[16] = {0};
    int squared[16] = {0};

    matrix_power(base, exp / 2, half);
    matrix_multiply(half, half, squared, 16);

    if (exp % 2 == 0)
        copy_matrix(squared, out, 16);
    else
        matrix_multiply(squared, base, out, 16);

    return;
}

/* Matrix-vector product, one row at a time. */
int matrix_vector_cell(int matrix[16], int vector[4],
                       unsigned row, unsigned remaining) {
    if (remaining == 0)
        return 0;

    unsigned k = 4 - remaining;
    return matrix[row * 4 + k] * vector[k]
        + matrix_vector_cell(matrix, vector, row, remaining - 1);
}

int matrix_vector_row(int matrix[16], int vector[4], unsigned row) {
    return matrix_vector_cell(matrix, vector, row, 4);
}

/* Weighted checksum detects misplaced or missing matrix entries. */
int weighted_checksum(int matrix[16], unsigned remaining) {
    if (remaining == 0)
        return 0;

    unsigned idx = 16 - remaining;
    return (idx + 1) * matrix[idx]
        + weighted_checksum(matrix, remaining - 1);
}

/* Sum of the Fibonacci block. */
int fibonacci_block_sum(int matrix[16]) {
    return matrix[0] + matrix[1] + matrix[4] + matrix[5];
}

/* Independent scalar recursion to add a second call tree. */
int power_of_two(unsigned n) {
    if (n == 0)
        return 1;
    return 2 * power_of_two(n - 1);
}

int factorial(unsigned n) {
    if (n <= 1)
        return 1;
    return n * factorial(n - 1);
}
