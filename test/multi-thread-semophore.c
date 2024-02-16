#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>

#define NUM_THREADS 3

pthread_mutex_t mutex;
sem_t semaphore;

// Shared resource
int shared_resource = 0;

void* thread_function(void* arg) {
    long tid = (long)arg;

    // Wait on semaphore
    sem_wait(&semaphore);

    // Lock mutex to modify shared resource
    pthread_mutex_lock(&mutex);
    printf("Thread %ld acquiring mutex\n", tid);
    shared_resource++;
    printf("Thread %ld modifying shared resource to %d\n", tid, shared_resource);
    pthread_mutex_unlock(&mutex);
    printf("Thread %ld released mutex\n", tid);

    // Post semaphore
    sem_post(&semaphore);

    pthread_exit(NULL);
}

int main() {
    pthread_t threads[NUM_THREADS];
    long t;

    // Initialize mutex and semaphore
    pthread_mutex_init(&mutex, NULL);
    sem_init(&semaphore, 0, 2); // Allow 2 threads to enter the critical section concurrently

    for(t = 0; t < NUM_THREADS; t++) {
        pthread_create(&threads[t], NULL, thread_function, (void*)t);
    }

    // Wait for threads to finish
    for(t = 0; t < NUM_THREADS; t++) {
        pthread_join(threads[t], NULL);
    }

    // Clean up
    pthread_mutex_destroy(&mutex);
    sem_destroy(&semaphore);

    printf("Final shared resource value: %d\n", shared_resource);

    return 0;
}