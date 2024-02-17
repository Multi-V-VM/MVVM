#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>

#define NUM_THREADS 4
#define ITERATIONS 1e3 // Define how many times each thread will modify the shared resource

pthread_mutex_t mutex;
sem_t semaphore;

// Shared resource
int shared_resource = 0;

void *thread_function(void *arg) {
    long tid = (long)arg;

    for (int i = 0; i < ITERATIONS; i++) { // Loop to interact with the shared resource multiple times
        // Wait on semaphore
        sem_wait(&semaphore);

        // Lock mutex to modify shared resource
        pthread_mutex_lock(&mutex);
        printf("Thread %ld acquiring mutex, iteration %d\n", tid, i+1);
        shared_resource++;
        printf("Thread %ld modifying shared resource to %d, iteration %d\n", tid, shared_resource, i+1);
        pthread_mutex_unlock(&mutex);
        printf("Thread %ld released mutex, iteration %d\n", tid, i+1);

        // Post semaphore
        sem_post(&semaphore);
    }

    return NULL;
}

int main() {
    pthread_t threads[NUM_THREADS];
    long t;

    // Initialize mutex and semaphore
    pthread_mutex_init(&mutex, NULL);
    sem_init(&semaphore, 0, 2); // Allow 2 threads to enter the critical section concurrently

    for (t = 0; t < NUM_THREADS; t++) {
        pthread_create(&threads[t], NULL, thread_function, (void *)t);
    }

    // Wait for threads to finish
    for (t = 0; t < NUM_THREADS; t++) {
        pthread_join(threads[t], NULL);
    }

    // Clean up
    pthread_mutex_destroy(&mutex);
    sem_destroy(&semaphore);

    printf("Final shared resource value: %d\n", shared_resource);

    return 0;
}