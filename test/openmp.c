#include <omp.h>
#include <stdio.h>
int threads=8;
int main(int argc, char** argv) {
#pragma omp parallel num_threads(8)
  { printf("Hello World... from thread = %d\n", omp_get_thread_num()); }
}
