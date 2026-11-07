__attribute__((import_module("env"), import_name("mvvm_checkpoint")))
extern void checkpoint(void);
static volatile unsigned effects;
__attribute__((noinline)) static unsigned leaf(unsigned x) {
    unsigned y=x*7+11;
    effects++;
    checkpoint();
    return y+effects;
}
__attribute__((noinline)) static unsigned middle(unsigned x) {
    unsigned kept=x+29;
    return kept+leaf(x+3);
}
static unsigned (*volatile indirect)(unsigned)=middle;
__attribute__((export_name("run"))) unsigned run(unsigned n) {
    unsigned acc=17;
    for(unsigned i=0;i<n;i++) acc+=indirect(i);
    return acc;
}
__attribute__((export_name("effects"))) unsigned effect_count(void) { return effects; }
