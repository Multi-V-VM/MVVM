#include <stdio.h>
#include <unistd.h>
int a(int c){
    static int b=0;
//    printf("\n");
}
int main(int argv,char ** argc){
    int c=0;
    int d = atoi(argc[1]);
    while (1){
        a(c);
        c++;
        sleep(1);
        if(c==d)
            break;
    }
//    __wasilibc_nocwd_openat_nomode(1,"/dev/stdout",0);
}
