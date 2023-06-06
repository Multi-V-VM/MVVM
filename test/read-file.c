#include <stdio.h>
#include <unistd.h>
int main() {
    FILE *file = fopen("text.txt", "w");
    sleep(1);
    fprintf(file, "Successfully wrote to the file.");
}