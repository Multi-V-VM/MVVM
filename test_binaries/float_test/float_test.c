//
// Created by noah on 01.10.20.
//

#include <time.h>
#include <stdio.h>
#include <stdlib.h>

void sort(float numbers[], size_t len);
void sort_internal(float numbers[], float buf[], size_t len, int l, int r);
void merge(float numbers[], float buf[], int l, int m, int r);
int chk_sort(float numbers[], size_t len);

int main(int argc, char **argv) {
    int len = 200;

    //check for any unsorted arrays
    int fail = 0;

    const size_t runs = 1000;
    double total = 0;
    for(size_t run = 0; run < runs; run++) {
        printf("Iteration %d... ", run + 1);

        //assign to the same random array (sorted in-place so every run needs a new array)
        float values[] = {145.72, 150.91, 25.84, 180.31, 110.33, 22.40, 171.12, 157.61, 133.23, 139.72, 189.24, 138.93, 61.74, 197.23, 144.44, 72.51,
                17502, 14631, 6534, 82.25, 195.86, 174.22, 120.89, 122.44, 87.95, 63.86, 123.74, 165.58, 198.02, 167.53, 47.11,
                18619, 14703, 16280, 15600, 16930, 1211.3, 51.47, 5283, 5718, 794, 11330, 14.629, 55.93, 181, 173.09,
                2963, 16091, 4275, 15549, 191.00, 7425, 184.69, 179.00, 17116, 18564, 9172, 410, 12815, 10612, 199.32,
                5450, 19308, 188, 10427, 119.41, 821.9, 5233, 86.53, 78.60, 128.07, 135.68, 26.80, 830.2, 119.79, 17.919,
                7799, 183.50, 16745, 2300, 7939, 820.5, 1324, 4669, 211, 17244, 10334, 9063, 17401, 8748, 14614, 4929,
                16314., 52.97, 18793, 16178, 7248, 14.343, 186.34, 109.66, 1257.9, 6114., 130.87, 1.7068, 1.5880, 9.361, 1.1037,
                12442, 30.01, 749, 527, 18.327, 882.4, 9.398, 6.35, 1305.9, 427.1, 199.4, 84.47, 12832, 1266, 18577, 19598,
                11.330, 14989, 18632, 1.2553, 14810., 5024., 26.2, 1960.9, 1.6836, 2906, 10.572, 12.138, 12669, 2746, 504,
                7931, 85.42, 15002, 10960, 76.45, 9422., 11924, 10834, 17165, 14082, 6988, 7811, 690, 2374, 4535,
                148.23, 10.216, 19651, 169.38, 18721., 57.04, 1101, 4086, 1.0485, 14759, 1.922, 8594., 13982, 6532, 5424,
                47.28, 16592, 10304, 14334, 14744, 3303, 6627, 5472, 18350, 4179, 8739, 73.09, 1415, 4614, 11953,
                9256, 14.340, 12.169, 9676, 1535, 3.30, 13904, 541.3, 11973, 13032, 14493, 182.07, 14298, 5783, 14870,
                404.1, 18581};

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

void sort(float numbers[], size_t len) {
    float buf[len];
    sort_internal(numbers, buf, len, 0, len - 1);
}

void sort_internal(float numbers[], float buf[], size_t len, int l, int r) {
    size_t part = r - l + 1;
    if (part <= 1) return;

    size_t ctr = (l + r) / 2;
    sort_internal(numbers, buf, len, l, ctr);
    sort_internal(numbers, buf, len, ctr + 1, r);
    merge(numbers, buf, l, ctr, r);
}

void merge(float numbers[], float buf[], int l, int m, int r) {
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
int chk_sort(float numbers[], size_t len) {
    for(size_t i = 1; i < len; i++) {
        if (numbers[i - 1] > numbers[i]) {
            return 1;
        }
    }

    return 0;
}
