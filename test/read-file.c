
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

int main() {
    FILE *file = fopen("./text.txt", "w");
    
    FILE *file1 = fopen("./text1.txt", "w");
    FILE *file2 = fopen("./text2.txt", "w");
    fseek(file,1,1);
    int c = 0;
    while (1){
        c++;
        if (c==100000){
            break;
        }
    }
    fprintf(file, "Successfully wrote to the file.");
    fclose(file);
    fclose(file1);
    fclose(file2);
}