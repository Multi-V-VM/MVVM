#include "openblas/cblas.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void init(double* matrix, int row, int column) {
    for (int j = 0; j < column; j++) {
        for (int i = 0; i < row; i++) {
            matrix[j * row + i] = ((double)rand()) / RAND_MAX;
        }
    }
}

int main(int argc, char* argv[]) {
    int rowsA, colsB, common;
    int i, j, k;
    clock_t start, end;
    double cpu_time_used;

    rowsA = 1e5;
    colsB = 1e4;
    common = 1e4;

    double A[rowsA * common];
    double B[common * colsB];
    double C[rowsA * colsB];

    srand(time(NULL));

    printf("Run,Time\n");

    for (i = 0; i < 100; i++) {
        init(A, rowsA, common);
        init(B, common, colsB);
        start = clock();
        cblas_dgemm(CblasColMajor, CblasNoTrans, CblasNoTrans, rowsA, colsB, common,
                    1.0, A, rowsA, B, common, 0.0, C, rowsA);
        end = clock();
        cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("%u,%f\n", i, cpu_time_used);
    }

    return 0;
}