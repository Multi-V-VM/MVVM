#include <stdio.h>
#include <unistd.h>
int main() {
    FILE *file = fopen("text.txt", "w");
    sleep(100);
    fprintf(file, "Successfully wrote to the file.");
}