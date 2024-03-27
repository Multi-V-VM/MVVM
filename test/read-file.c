#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <unistd.h>
// int o_(char *path,int fd) {
//     int flags = O_RDWR | O_CREAT;
//     printf("open %s, %d\n", path, fd);
//     int ret = open( path, flags);
//     printf("ret %d %s %d\n", ret,path,errno);

//     if (ret !=fd)
//         __wasi_fd_renumber(ret, fd);
//     printf("open %s, ret = %d\n", path, fd);
//     return ret;
// }
int o_(char *path, int fd) {
    int flags = O_RDWR | O_CREAT;
    int *failed_array = (int *)malloc(100 * sizeof(int));
    int counter = 0;
    int ret = open(path, flags);
    printf("ret %d %s %d\n", ret, path, errno);
    while (ret != fd) {
        failed_array[counter] = ret;
        counter += 1;
        ret = open(path, flags);
        printf("ret %d %s %d\n", ret, path, errno);
    }
    for (int i = 0; i < counter; i++) {
        close(failed_array[i]);
    }
    free(failed_array);
    return ret;
}
FILE *a_file = NULL;

int main() {
    // FILE *file = fopen("./text.txt", "w");
    char *buffer = (char *)malloc(16);
    /** Test fread*/
    FILE *file3 = fopen("./test3.txt", "rw+");

    const char *line1 = "This is line 1\n";
    const char *line2 = "This is line 2\n";
    const char *line3 = "This is line 3\n";
    size_t len1 = strlen(line1);
    size_t len2 = strlen(line2);
    printf("File pointer: 0\n");

    fwrite(line1, sizeof(char), len1, file3);
    // ntwritten
    printf("File pointer: %ld\n", ftell(file3));

    //  checkpoint: lib_wasi_wrapper fwrite system record
    fwrite(line2, sizeof(char), len2, file3);

    printf("File pointer: %ld\n", ftell(file3));
    // fseek(file3, -30, SEEK_CUR);
    rewind(file3);
    printf("File pointer: %ld\n", ftell(file3));

    // Allocate memory to contain the whole file
    buffer = (char *)malloc(sizeof(char) * 30);
    if (buffer == NULL) {
        fputs("Memory error", stderr);
        exit(2);
    }

    // Read the file into the buffer
    int result = fread(buffer, 1, 30, file3);
    if (result != 30) {
        fputs("Reading error", stderr);
        exit(3);
    }
    // buffer = line2[0:len2-1
    printf("%d %s\n", result, buffer);
    fseek(file3, -15, SEEK_CUR);

    printf("File pointer: %ld\n", ftell(file3));
    fwrite(line3, sizeof(char), len2, file3);
    printf("File pointer: %ld\n", ftell(file3));

    // rename("./test3.txt", "./test4.txt");
    // fclose(file);
    fclose(file3);
}
