/*
 * Copyright (C) 2023 Amazon.com Inc. or its affiliates. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <pthread.h>
#include <stdio.h>

#define MAX_NUM_THREADS 4
#define NUM_ITER 1000

int g_count = 0;
pthread_mutex_t g_count_lock;
static void *thread(void *arg) {
    for (int i = 0; i < NUM_ITER; i++) {
        __atomic_fetch_add(&g_count, 1, __ATOMIC_SEQ_CST);
        printf("print!!!%d\n", g_count);
    }
    printf("Value of g_count is %d\n", g_count);
    printf("%d\n", g_count);

    return NULL;
}

int main(int argc, char **argv) {
    pthread_t tids[MAX_NUM_THREADS];
    g_count_lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;

    for (int i = 0; i < MAX_NUM_THREADS; i++) {
        if (pthread_create(&tids[i], NULL, thread, NULL) != 0) {
            printf("Thread creation failed\n");
        }
    }
    printf("Value %d\n", g_count);

    for (int i = 0; i < MAX_NUM_THREADS; i++) {
        if (pthread_join(tids[i], NULL) != 0) {
            printf("Thread join failed\n");
        }
    }

    printf("Value of counter after update: %d (expected=%d)\n", g_count, MAX_NUM_THREADS * NUM_ITER);
    FILE *f = fopen("./test1.txt", "w");

    fprintf(f, "%d\n", g_count);

    if (g_count != MAX_NUM_THREADS * NUM_ITER) {
        __builtin_trap();
    }
    __wasilibc_nocwd_openat_nomode(1, "/dev/stdout", 0);
    exit(0);
}
