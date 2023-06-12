#include <stdio.h>
#include <unistd.h>
int main() {
    FILE *file = fopen("./text.txt", "w");
    FILE *file1 = fopen("./text1.txt", "w");
    FILE *file2 = fopen("./text2.txt", "w");
    int c = 0;
    while (1){
        c++;
        if (c==10){
            break;
        }
    }
    fprintf(file, "Successfully wrote to the file.");
    fclose(file);
    fclose(file1);
    fclose(file2);
}