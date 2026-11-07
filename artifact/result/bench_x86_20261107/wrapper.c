#include <stdio.h>
unsigned suite_verified;
extern int bench_main(int,char **);
__attribute__((export_name("run"))) unsigned run(unsigned n) {
    (void)n; char arg[]="benchmark"; char *args[]={arg,0};
    bench_main(1,args); fflush(stdout);
    return suite_verified ? 0x1234 : 0xffffffff;
}
__attribute__((export_name("effects"))) unsigned effects(void) { return suite_verified; }
