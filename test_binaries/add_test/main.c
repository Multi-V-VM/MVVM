//
// Created by jhne on 10/5/20.
//

#include <stdio.h>

int a_int = 13;

__attribute_noinline__ void inc() {
    a_int += 3;
}

int main() {
    printf("a_int = %d\n", a_int);
    inc();
    printf("a_int = %d\n", a_int);
    inc();
    printf("a_int = %d\n", a_int);
}
