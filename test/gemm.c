#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

void dgemm(int8_t a, int8_t b, int8_t c, int32_t d, int32_t e, int32_t f,
    double g, double *h, int32_t i, double *j, int32_t k, double l, double *m, int32_t n);

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

//    srand(time(NULL));

    printf("Run,Time\n");

    for (i = 0; i < 1e8; i++) {
        init(A, rowsA, common);
        init(B, common, colsB);
        start = clock();
        dgemm(1, 1, 1, rowsA, colsB, common,
                    1.0, A, rowsA, B, common, 0.0, C, rowsA);
        end = clock();
        cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("%u,%f\n", i, cpu_time_used);
    }

    return 0;
}