#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/fcntl.h>
#include <unistd.h>

FILE *a_file = NULL;

int main() {
    FILE *file = fopen("./text.txt", "a");

    FILE *file1 = fopen("./text1.txt", "w");
    FILE *file2 = fopen("./text2.txt", "w");
    // fseek(file,1,1);

    /** Test fread*/
    volatile int c = 0;
    FILE *file3 = fopen("./test3.txt", "a");
    const char *line1 = "This is line 1\n";
    const char *line2 = "This is line 2\n";
    size_t len1 = strlen(line1);
    size_t len2 = strlen(line2);

    fwrite(line1, sizeof(char), len1, file3);
    // ntwritten
    //  checkpoint: lib_wasi_wrapper fwrite system record
    for (int i = 0; i < 10000; i++) {
        c++;
    }
    fwrite(line2, sizeof(char), len2, file3);

    for (int i = 0; i < 10000; i++) {
        c++;
    }
    fseek(file3, 1, SEEK_END);
    for (int i = 0; i < 10000; i++) {
        c++;
    }
    fread(file3, sizeof(char), len2, file3);
    ftell(file3);
    renameat(4, "./test3.txt", 10, "./test4.txt");
    fclose(file);
    fclose(file1);
    fclose(file2);
    fclose(file3);
    __wasi_fd_renumber(3, 10);
}
