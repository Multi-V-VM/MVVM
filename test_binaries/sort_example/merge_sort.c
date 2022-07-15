//
// Created by flo on 15.07.20.
//

#include <time.h>
#include <stdio.h>
#include <stdlib.h>

void sort(int numbers[], size_t len);
void sort_internal(int numbers[], int buf[], size_t len, int l, int r);
void merge(int numbers[], int buf[], int l, int m, int r);
int chk_sort(int numbers[], size_t len);

int main(int argc, char **argv) {
    int len = 200;

    //check for any unsorted arrays
    int fail = 0;

    const size_t runs = 1000;
    double total = 0;
    for(size_t run = 0; run < runs; run++) {
        printf("Iteration %d... ", run + 1);

        //assign to the same random array (sorted in-place so every run needs a new array)
        int values[] = {14572, 15091, 2584, 18031, 11033, 2240, 17112, 15761, 13323, 13972, 18924, 13893, 6174, 19723, 14444, 7251,
                17502, 14631, 6534, 8225, 19586, 17422, 12089, 12244, 8795, 6386, 12374, 16558, 19802, 16753, 4711,
                18619, 14703, 16280, 15600, 16930, 12113, 5147, 5283, 5718, 794, 11330, 14629, 5593, 181, 17309,
                2963, 16091, 4275, 15549, 19100, 7425, 18469, 17900, 17116, 18564, 9172, 410, 12815, 10612, 19932,
                5450, 19308, 188, 10427, 11941, 8219, 5233, 8653, 7860, 12807, 13568, 2680, 8302, 11979, 17919,
                7799, 18350, 16745, 2300, 7939, 8205, 1324, 4669, 211, 17244, 10334, 9063, 17401, 8748, 14614, 4929,
                16314, 5297, 18793, 16178, 7248, 14343, 18634, 10966, 12579, 6114, 13087, 17068, 15880, 9361, 11037,
                12442, 3001, 749, 527, 18327, 8824, 9398, 635, 13059, 4271, 1994, 8447, 12832, 1266, 18577, 19598,
                11330, 14989, 18632, 12553, 14810, 5024, 262, 19609, 16836, 2906, 10572, 12138, 12669, 2746, 504,
                7931, 8542, 15002, 10960, 7645, 9422, 11924, 10834, 17165, 14082, 6988, 7811, 690, 2374, 4535,
                14823, 10216, 19651, 16938, 18721, 5704, 1101, 4086, 10485, 14759, 1922, 8594, 13982, 6532, 5424,
                4728, 16592, 10304, 14334, 14744, 3303, 6627, 5472, 18350, 4179, 8739, 7309, 1415, 4614, 11953,
                9256, 14340, 12169, 9676, 1535, 330, 13904, 5413, 11973, 13032, 14493, 18207, 14298, 5783, 14870,
                4041, 18581};

        struct timespec start;
        clock_gettime(CLOCK_MONOTONIC, &start);

        sort(values, len);

        struct timespec end;
        clock_gettime(CLOCK_MONOTONIC, &end);

        double nanos = 1e9 * (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec);

        printf("\twith execution time %f nanoseconds...", nanos);
        total += nanos;

        int verified = chk_sort(values, len);
        if (verified == 0) {
            printf("OK\n");
        } else {
            printf("FAIL\n");
        }

        fail |= verified;
    }

    double avg = total / (double) runs;

    printf("Average execution time: %f nanoseconds.\n", avg);

    if (fail == 1) {
        printf("There were test failures! See above.\n");
    }

    exit(fail);
}

void sort(int numbers[], size_t len) {
    int buf[len];
    sort_internal(numbers, buf, len, 0, len - 1);
}

void sort_internal(int numbers[], int buf[], size_t len, int l, int r) {
    size_t part = r - l + 1;
    if (part <= 1) return;

    size_t ctr = (l + r) / 2;
    sort_internal(numbers, buf, len, l, ctr);
    sort_internal(numbers, buf, len, ctr + 1, r);
    merge(numbers, buf, l, ctr, r);
}

void merge(int numbers[], int buf[], int l, int m, int r) {
    size_t part = r - l + 1;
    size_t lo = l;
    size_t hi = m + 1;
    size_t idx = 0;

    if (part < 1) return;

    while (lo <= m && hi <= r) {
        if (numbers[lo] <= numbers[hi]) {
            buf[idx++] = numbers[lo++];
        } else {
            buf[idx++] = numbers[hi++];
        }
    }

    while(lo <= m) {
        buf[idx++] = numbers[lo++];
    }

    while(hi <= r) {
        buf[idx++] = numbers[hi++];
    }

    for(size_t i = 0; i < part; ++i) {
        numbers[l + i] = buf[i];
    }
}

/**
 * Checks if the passed array of numbers is sorted in an ascending manner.
 * @param numbers the array to check
 * @param len the length of the buffer
 * @return 0 if the array is sorted, else 1
 */
int chk_sort(int numbers[], size_t len) {
    for(size_t i = 1; i < len; i++) {
        if (numbers[i - 1] > numbers[i]) {
            return 1;
        }
    }

    return 0;
}
