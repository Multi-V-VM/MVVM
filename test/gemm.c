#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

void dgemm(int8_t a, int8_t b, int8_t c, int32_t d, int32_t e, int32_t f,
    double g, double *h, int32_t i, double *j, int32_t k, double l, double *m, int32_t n);

void init(double* matrix, int row, int column) {
    for (int j = 0; j < column; j++) {
        for (int i = 0; i < row; i++) {
            matrix[j * row + i] = i;
        }
    }
}

int main(int argc, char* argv[]) {
    int rowsA, colsB, common;
    int i, j, k;

    rowsA = 100;
    colsB = 100;
    common = 100;

    double A[rowsA * common];
    double B[common * colsB];
    double C[rowsA * colsB];

    printf("Run,Time\n");

    for (i = 0; i < 1e8; i++) {
        init(A, rowsA, common);
        init(B, common, colsB);
        dgemm(1, 1, 1, rowsA, colsB, common,
                    1.0, A, rowsA, B, common, 0.0, C, rowsA);
    }

    return 0;
}