#include <stdio.h>
#include <unistd.h>
int a(int c){
    static int b=0;
    printf("%d %d\n",b,c);
}
int main(int argv,char ** argc){
    int c=0;
    int d = atoi(argc[0]);
    while (1){
        a(c);
        c++;
        sleep(1);
        if(c==d)
            break;
    }
}
