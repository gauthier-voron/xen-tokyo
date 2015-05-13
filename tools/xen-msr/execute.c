#include <alloca.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "xen-msr.h"


unsigned long now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	return ts.tv_sec * 1000 + ts.tv_nsec / 1000000ul;
}

void execute(const struct command *cmds, unsigned long cmdc,
	     const unsigned long *cores, unsigned long corec)
{
	unsigned long i, j;
	unsigned long current, nearest, start = now();
	unsigned long *next = alloca(cmdc * sizeof(unsigned long));
	unsigned long *values = alloca(corec * sizeof(unsigned long));
	struct timespec ts;

	printf("time ");
	for (i=0; i<cmdc; i++) {
		if (cmds[i].fields & FIELD_TIME)
			next[i] = start + cmds[i].time;
		else
			next[i] = start;
		if (cmds[i].fields & FIELD_PRINT)
			for (j=0; j<corec; j++)
				printf("0x%lx(%lu) ", cmds[i].addr, cores[j]);
	}
	printf("\n");

	while (1) {
		current = now();
		printf("%lu.%03lu ", (current - start) / 1000,
		       (current - start) % 1000);
		for (i=0; i<cmdc; i++) {
			if (next[i] > current || next[i] < start) {
				if (cmds[i].fields & FIELD_PRINT)
					for (j=0; j<corec; j++)
						printf("- ");
				continue;
			}

			if (cmds[i].fields & FIELD_VALUE)
				hypercall_wrmsr(cmds[i].addr, cmds[i].value,
						corec, cores, values);
			else
				hypercall_rdmsr(cmds[i].addr, corec, cores,
						values);

			if (cmds[i].fields & FIELD_PRINT)
				for (j=0; j<corec; j++)
					printf("%lu ", values[j]);

			if (cmds[i].fields & FIELD_INTERVAL) {
				while (next[i] <= current)
					next[i] += cmds[i].interval;
			} else {
				next[i] = 0;
			}
		}
		printf("\n");
		fflush(stdout);

		nearest = ~(0ul);
		for (i=0; i<cmdc; i++)
			if (next[i] > current && next[i] < nearest)
				nearest = next[i];
		if (nearest == ~(0ul))
			break;

		ts.tv_sec = (nearest-current) / 1000;
		ts.tv_nsec = ((nearest-current) - ts.tv_sec * 1000) * 1000000;
		nanosleep(&ts, NULL);
	}
}
