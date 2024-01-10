#include <omp.h>
#include <stdio.h>
#include <stdbool.h>
int a1=0;
int main(int argc, char** argv) {
  int a [100] = {0};
#pragma omp parallel for num_threads(8)
  { 
    for (int i = 0; i < 100; i++) {
      a[i] = i;
      printf("%d\n", a[i]);
      while (true){
        if (a1 % 10000==0)
          break;
        a1=a1+1;
      }
    }
  }
  
}
