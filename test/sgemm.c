#include <stdio.h>
#include <stdlib.h>

void sgemm(int m, int n, int k, const float *__restrict__ A, const float *__restrict__ B, float *__restrict__ C) {
    int i, j, p;
    for (i = 0; i < m; i++) {
        for (j = 0; j < n; j++) {
            float cij = C[i * n + j];
            for (p = 0; p < k; p++) {
                cij += A[i * k + p] * B[p * n + j];
            }
            C[i * n + j] = cij;
        }
    }
}

void init(float *matrix, int row, int column) {
    for (int j = 0; j < column; j++) {
        for (int i = 0; i < row; i++) {
            matrix[j * row + i] = (double)(rand());
        }
    }
}

int main(int argc, char *argv[]) {
    int rowsA, colsB, common;
    int i;

    rowsA = 512;
    colsB = 512;
    common = 512;

    float *A = (float *)malloc(rowsA * common * sizeof(float));
    float *B = (float *)malloc(common * colsB * sizeof(float));
    float *C = (float *)malloc(rowsA * colsB * sizeof(float));

    init(A, rowsA, common);
    init(B, common, colsB);
    for (i = 0; i < 10; i++) {
        sgemm(rowsA, colsB, common, A, B, C);
    }

    printf("%f\n", C[0]);
    free(A);
    free(B);
    free(C);

    return 0;
}