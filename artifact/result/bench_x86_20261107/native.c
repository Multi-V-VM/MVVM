// Native GCC comparator for the same checkpoint-instrumented algorithm sources.
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

extern unsigned run(unsigned);
#ifdef INDIRECT_WORKLOAD
extern unsigned effect_count(void);
void checkpoint(void) {}
#define effect_total effect_count
#else
extern unsigned effects(void);
#define effect_total effects
#endif

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    char *end;
    unsigned long count = strtoul(argv[1], &end, 10);
    if (*end || !count || count > UINT32_MAX) return 2;
    struct timespec begin, finish;
    clock_gettime(CLOCK_MONOTONIC, &begin);
    unsigned result = run((unsigned)count);
    clock_gettime(CLOCK_MONOTONIC, &finish);
    double ms = (finish.tv_sec - begin.tv_sec) * 1000.0 +
                (finish.tv_nsec - begin.tv_nsec) / 1000000.0;
    printf("{\"result\":%u,\"effects\":%u,\"run_ms\":%.6f}\n",
           result, effect_total(), ms);
    return result == UINT32_MAX ? 1 : 0;
}
