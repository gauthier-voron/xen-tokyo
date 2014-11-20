#ifndef XEN_MSR_H
#define XEN_MSR_H


void fatal(const char *format, ...);


#define FIELD_ADDR       (1 << 0)
#define FIELD_PRINT      (1 << 1)
#define FIELD_VALUE      (1 << 2)
#define FIELD_TIME       (1 << 3)
#define FIELD_INTERVAL   (1 << 4)

struct command
{
	unsigned long fields;
	unsigned long addr;
	unsigned long value;
	unsigned long time;
	unsigned long interval;
};

unsigned long parse_number(const char *str, char **end);

int parse_command(struct command *dest, const char *argv);


void execute(const struct command *cmds, unsigned long cmdc,
	     const unsigned long *cores, unsigned long corec);


void hypercall_wrmsr(unsigned long addr, unsigned long value,
		     unsigned long size, const unsigned long *cores,
		     unsigned long *values);

void hypercall_rdmsr(unsigned long addr, unsigned long size,
		     const unsigned long *cores, unsigned long *values);


#endif
