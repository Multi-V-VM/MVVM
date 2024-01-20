/*
 * Copyright (C) 2023 Amazon.com Inc. or its affiliates. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <pthread.h>
#include <stdio.h>

#define MAX_NUM_THREADS 2
#define NUM_ITER 10000

int g_count = 0;
pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;

static void *thread(void *arg) {
    for (int i = 0; i < NUM_ITER; i++) {
	pthread_mutex_lock(&m);
	g_count++;
	pthread_mutex_unlock(&m);
    }
    printf("Value of g_count is %d\n", g_count);
    printf("%d\n", g_count);
    return NULL;
}

int main(int argc, char **argv) {
	pthread_mutex_init(&m,NULL);
    pthread_t tids[MAX_NUM_THREADS];

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
    if (g_count != MAX_NUM_THREADS * NUM_ITER) {
        __builtin_trap();
    }
    __wasilibc_nocwd_openat_nomode(1,"/dev/stdout",0);
    return -1;
}
