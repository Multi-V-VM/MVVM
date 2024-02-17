#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Global mutex and condition variable
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

// Shared variable among threads to demonstrate the condition change
int ready = 0;

// Worker thread function
void* worker(void* arg) {
    pthread_mutex_lock(&mutex);
    while (!ready) {
        printf("Worker %ld is waiting for the signal.\n", (long)arg);
        pthread_cond_wait(&cond, &mutex);
    }
    printf("Worker %ld received the signal.\n", (long)arg);
    pthread_mutex_unlock(&mutex);
    return NULL;
}

int main(int argc, char* argv[]) {
    int num_threads = 5; // Number of worker threads
    pthread_t threads[num_threads];

    // Create worker threads
    for (long i = 0; i < num_threads; i++) {
        if (pthread_create(&threads[i], NULL, worker, (void*)i) != 0) {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
    }

    // Sleep for a bit to ensure all workers are waiting
    sleep(1);

    // Broadcast to all waiting threads
    pthread_mutex_lock(&mutex);
    ready = 1;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&mutex);
    printf("Signal sent to all waiting workers.\n");

    // Join all threads
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    // Clean up
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);

    return 0;
}