#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

int main() {
    int num;
    FILE *fptr;

    // use appropriate location if you are using MacOS or Linux
    fptr = fopen("program.txt", "w");

    if (fptr == NULL) {
        printf("Error! %s (%i)\n", strerror(errno), errno);
        exit(1);
    }

    printf("Enter num: \n");
    fflush(stdout);
    scanf("%d", &num);

    fprintf(fptr, "%d\n", num);
    fclose(fptr);

    return 0;
}
