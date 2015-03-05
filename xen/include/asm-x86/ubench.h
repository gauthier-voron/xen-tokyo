#ifndef __ASM_X86_UBENCH_H__
#define __ASM_X86_UBENCH_H__


#define UBENCH_MAX_BENCHES    64


int prepare_ubench(int node, unsigned long size, unsigned long time);

void affect_ubench(int bd, int core);

void run_ubench(int bd);

void finalize_ubench(int bd);


#endif
