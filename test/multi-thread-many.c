#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define NUM_THREADS 5

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
int ready = 0; // Condition variable to wait for

void* thread_func(void* arg) {
    pthread_mutex_lock(&mutex);
    while (!ready) {
        printf("Thread %ld waiting on condition variable.\n", (long)arg);
        pthread_cond_wait(&cond, &mutex);
    }
    pthread_mutex_unlock(&mutex);
    printf("Thread %ld received the broadcast.\n", (long)arg);
    return NULL;
}

int main() {
    pthread_t threads[NUM_THREADS];
    long i;

    // Create threads
    for (i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, thread_func, (void*)i) != 0) {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
    }

    // Sleep for a bit to ensure all threads are waiting
    sleep(1);

    // Change the condition and broadcast to all waiting threads
    pthread_mutex_lock(&mutex);
    ready = 1;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&mutex);
    printf("Broadcast sent.\n");

    // Join threads
    for (i = 0; i < NUM_THREADS; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            perror("pthread_join");
            exit(EXIT_FAILURE);
        }
    }

    // Cleanup
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);

    return 0;
}