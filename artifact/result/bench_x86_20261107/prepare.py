from pathlib import Path
import shutil, subprocess
base=Path('/root/mvvm-bench-suite'); src=base/'source'; work=base/'prepared'
shutil.copytree(src,work,dirs_exist_ok=True)
nas=work/'nas'
subprocess.run(['gcc-16','-O2','-std=gnu89',str(nas/'sys/setparams.c'),'-o',str(base/'setparams')],check=True)
for name in ['CG','EP','FT','MG','BT','LU','SP','IS']:
    (nas/name/'npbparams.h').unlink()
    subprocess.run([str(base/'setparams'),name.lower(),'W'],cwd=nas/name,check=True)
    p=nas/name/(name.lower()+'.c'); s=p.read_text(); assert 'int main(' in s
    p.write_text(s.replace('int main(', 'int bench_main(',1))
p=nas/'IS/is.c'
prototypes=(nas/'common/npb-C.h').read_text().split('extern void timer_clear')[1]
p.write_text('extern void timer_clear'+prototypes+'\n'+p.read_text())
p=nas/'common/c_print_results.c';s=p.read_text();s=s.replace('    char *evalue="1000";', '    extern unsigned suite_verified;\n    suite_verified = passed_verification > 0;\n    char *evalue="1000";');p.write_text(s)
(base/'wrapper.c').write_text('''#include <stdio.h>
unsigned suite_verified;
extern int bench_main(int,char **);
__attribute__((export_name("run"))) unsigned run(unsigned n) {
    (void)n; char arg[]="benchmark"; char *args[]={arg,0};
    bench_main(1,args); fflush(stdout);
    return suite_verified ? 0x1234 : 0xffffffff;
}
__attribute__((export_name("effects"))) unsigned effects(void) { return suite_verified; }
''')
# Fixed-work LINPACK adapter retains the repository's numerical kernels.
p=work/'linpack/linpack.c';s=p.read_text().replace('int main(void)','int unused_original_main(void)',1)
s+='''
static unsigned suite_checks;
__attribute__((export_name("run"))) unsigned run(unsigned reps) {
    const int n=200, lda=400;
    REAL *a=malloc((size_t)lda*lda*sizeof(REAL));
    REAL *b=malloc(lda*sizeof(REAL)); int *p=malloc(lda*sizeof(int));
    if(!a||!b||!p) return 0xffffffff;
    for(unsigned r=0;r<reps;++r) for(int roll=0;roll<2;++roll) {
        REAL norma; int info; matgen(a,lda,n,b,&norma);
        dgefa(a,lda,n,p,&info,roll); if(info) return 0xffffffff;
        dgesl(a,lda,n,p,b,0,roll);
        for(int i=0;i<n;++i) if(fabs(b[i]-1.0)>1.e-9) return 0xffffffff;
        ++suite_checks;
    }
    free(a);free(b);free(p);return 0x1234;
}
__attribute__((export_name("effects"))) unsigned effects(void) {return suite_checks;}
''';p.write_text(s)
