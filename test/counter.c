#include <stdio.h>
#include <unistd.h>
int a(int c){
    static int b=0;
    printf("%d %d\n",b,c);
}
int main(){
    int c;
    while (1){
        a(c);
        c++;
        sleep(1);
    }
}