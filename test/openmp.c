#include <omp.h>
#include <stdio.h>

int g_count = 0;

int main(int argc, char **argv) {
#pragma omp parallel
    {
        printf("Hello World... from thread = %d\n", omp_get_thread_num());
        for (int i = 0; i < 1000; i++) {
            __atomic_fetch_add(&g_count, 1, __ATOMIC_SEQ_CST);
            printf("print!!!%d\n", i);
        }
        printf("Value of g_count is %d\n", g_count);
        printf("%d\n", g_count);
    }
}
