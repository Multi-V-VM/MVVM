#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void dgemm(int ,int ,int,double g, double *h, int32_t i, double *j, int32_t k, double l, double *m, int32_t n);

void init(double *matrix, int row, int column) {
    for (int j = 0; j < column; j++) {
        for (int i = 0; i < row; i++) {
            matrix[j * row + i] = (double)(rand());
        }
    }
}

int main(int argc, char *argv[]) {
    int rowsA, colsB, common;
    int i, j, k;

    rowsA = 1000;
    colsB = 100000;
    common = 1000;

    double *A=(double*)malloc(rowsA * common*sizeof(double));
    double *B=(double*)malloc(common * colsB*sizeof(double));
    double *C=(double*)malloc(rowsA * colsB*sizeof(double));

    init(A, rowsA, common);
    init(B, common, colsB);
    for (i = 0; i < 1000; i++) {
        dgemm(rowsA,common,colsB,1.0, A, rowsA, B, common, 0.0, C, rowsA);
    }

    return 0;
}