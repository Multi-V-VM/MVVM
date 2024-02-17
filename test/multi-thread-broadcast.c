#include <pthread.h>
#include <stdio.h>
#include <unistd.h> // For sleep()

#define NUM_THREADS 4
#define READY_TIMES 100 // Number of times the ready condition is set

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
int ready = 0; // Condition variable to wait for

void* thread_func(void* arg) {
    for (int i = 0; i < READY_TIMES; ++i) {
        pthread_mutex_lock(&mutex);
        while (ready <= i) {
            printf("Thread %ld waiting on condition variable, iteration %d.\n", (long)arg, i+1);
            pthread_cond_wait(&cond, &mutex);
        }
        printf("Thread %ld received the broadcast, iteration %d.\n", (long)arg, i+1);
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}

int main() {
    pthread_t threads[NUM_THREADS];
    for (long i = 0; i < NUM_THREADS; ++i) {
        pthread_create(&threads[i], NULL, thread_func, (void*)i);
    }

    // Allow threads to start and wait
    sleep(1); // Simulate some work

    for (int i = 0; i < READY_TIMES; ++i) {
        pthread_mutex_lock(&mutex);
        ready++; // Update the condition
        pthread_cond_broadcast(&cond); // Notify all waiting threads
        pthread_mutex_unlock(&mutex);

        printf("Main thread broadcasted condition variable, iteration %d.\n", i+1);
        sleep(1); // Simulate time between condition changes
    }

    // Wait for all threads to complete
    for (int i = 0; i < NUM_THREADS; ++i) {
        pthread_join(threads[i], NULL);
    }

    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);

    return 0;
}