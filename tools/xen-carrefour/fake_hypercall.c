#include <alloca.h>
#include <stdio.h>
#include <string.h>


void wrmsr(unsigned long addr, unsigned long value, int cpu)
{
	if (0)
		printf("wrmsr(%lx, %lx, %d)\n", addr, value, cpu);
}

unsigned long rdmsr(unsigned long addr, int cpu)
{
	if (0)
		printf("rdmsr(%lx, %d)\n", addr, cpu);
	return 42;
}

double musage(void)
{
	return 0;
}

int xen_carrefour_send(const char *str, unsigned long count)
{
	return 0;
}
