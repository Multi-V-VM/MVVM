/*
 * Copyright (C) 2023 Amazon.com Inc. or its affiliates. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_NUM_THREADS 2
#define NUM_ITER 1000

int g_count = 0;
int *count;

static void *thread(void *arg) {
    for (int i = 0; i < NUM_ITER; i++) {
        __atomic_fetch_add(&g_count, 1, __ATOMIC_SEQ_CST);
    }
    printf("Value %d\n", g_count);
    for (int i =0; i<1e4;i++){
        count[i] = i;
        printf("Value %d\n", count[i]);
    }
    printf("%d\n", g_count);
    return NULL;
}

int main(int argc, char **argv) {
    pthread_t tids[MAX_NUM_THREADS];
    count = (int*)malloc(MAX_NUM_THREADS*sizeof(int));
    for (int i = 0; i < MAX_NUM_THREADS; i++) {
        if (pthread_create(&tids[i], NULL, thread, NULL) != 0) {
            printf("Thread creation failed\n");
        }
    }

    for (int i = 0; i < MAX_NUM_THREADS; i++) {
        if (pthread_join(tids[i], NULL) != 0) {
            printf("Thread join failed\n");
        }
    }

    printf("Value of counter after update: %d (expected=%d)\n", g_count, MAX_NUM_THREADS * NUM_ITER);
    if (g_count != MAX_NUM_THREADS * NUM_ITER) {
        __builtin_trap();
    }

    return -1;
}
