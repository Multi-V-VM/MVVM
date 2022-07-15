//
// Created by flo on 23.06.20.
//

#include "rv64minilibc.h"
#include <stddef.h>

void m_exit(int status) {
    register int code asm("a0");
    code = status;

    asm volatile (
    "addi a7, x0, 93\n\t"
    "ecall"
    ::: "memory"
    );
}

size_t m_strlen(char* str) {
    size_t len;
    for (len = 0; str[len] != '\0'; ++len) {
        ;
    }
    return len;
}

void m_write(int fd, void *buf, size_t len) {
    register int file asm("a0");
    file = fd;

    register void* string asm("a1");
    string = buf;

    register size_t length asm("a2");
    length = len;

    asm volatile (
    "addi a7, x0, 64\n\t"
    "ecall"
    ::: "memory"
    );
}

void m_putchar(char ch) {
    m_write(1, &ch, 1);
}

void printi(size_t i) {
    if (i > 9) {
        size_t rem = i / 10;
        i -= 10 * rem;
        printi(rem);
    }

    m_putchar('0' + i);
}

void print(char* buf) {
    m_write(1, buf, m_strlen(buf));
}

void print_arr(int* buf, size_t len) {
    print("[");
    for(size_t i = 0; i < len; ++i) {
        printi(buf[i]);
        if (i != len - 1) print(", ");
    }
    print("]\n");
}
