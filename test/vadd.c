#include <stdlib.h>
#include <stdio.h>

#define N 1000000

void vector_add(const float* __restrict__ a, const float * __restrict__ b, float* __restrict__ c) {
    for (int i = 0; i < N; i++) {
        c[i] = a[i] + b[i];
    }
}

int main() {
    float *a, *b, *c;
    a = (float*)malloc(N * sizeof(float));
    b = (float*)malloc(N * sizeof(float));
    c = (float*)malloc(N * sizeof(float));

    for (int i = 0; i < 10; i++) {
        vector_add(a, b, c);
    }

    fprintf(stderr, "c[0] = %f\n", c[0]);

    free(a);
    free(b);
    free(c);
    return 0;   
}