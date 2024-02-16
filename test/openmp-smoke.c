#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define NUM_THREADS 4

// Simulating __kmp_gtid_threadprivate_key
pthread_key_t gtid_key;

void* thread_func(void* arg) {
    long gtid = (long)arg; // Simulating global thread ID
    void* expected_value = (void*)(intptr_t)(gtid + 1);
    
    // Set thread-specific value
    int status = pthread_setspecific(gtid_key, expected_value);
    if (status != 0) {
        printf("Error setting thread-specific data\n");
        pthread_exit((void*)-1);
    }

    // Retrieve and verify thread-specific value
    void* value = pthread_getspecific(gtid_key);
    if (value != expected_value) {
        printf("Thread-specific data does not match (Thread %ld)\n", gtid);
        pthread_exit((void*)-1);
    } else {
        printf("Thread-specific data matches as expected (Thread %ld)\n", gtid);
    }

    pthread_exit((void*)0);
}

int main() {
    pthread_t threads[NUM_THREADS];
    int rc;
    long t;

    // Create key for thread-specific data
    pthread_key_create(&gtid_key, NULL);

    for (t = 0; t < NUM_THREADS; t++) {
        rc = pthread_create(&threads[t], NULL, thread_func, (void*)t);
        if (rc) {
            printf("ERROR; return code from pthread_create() is %d\n", rc);
            exit(-1);
        }
    }

    // Wait for threads to complete
    for (t = 0; t < NUM_THREADS; t++) {
        pthread_join(threads[t], NULL);
    }

    // Cleanup
    pthread_key_delete(gtid_key);

    return 0;
}