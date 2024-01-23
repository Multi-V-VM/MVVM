#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void dgemm(double g, double *h, int32_t i, double *j, int32_t k, double l, double *m, int32_t n);

void init(double *matrix, int row, int column) {
    for (int j = 0; j < column; j++) {
        for (int i = 0; i < row; i++) {
            matrix[j * row + i] = i;
        }
    }
}

int main(int argc, char *argv[]) {
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
        dgemm(1.0, A, rowsA, B, common, 0.0, C, rowsA);
    }

    return 0;
}